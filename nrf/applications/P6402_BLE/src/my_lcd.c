/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_lcd.c
**文件描述:        LCD显示屏(ST7735)基础显示框架实现
**当前版本:        V1.0
**作    者:       周森达 (zhousenda@jimiiot.com)
**完成日期:        2026.08.06
*********************************************************************
** 功能描述:        LCD显示屏基础显示框架实现
**                 1. 通过 MIPI-DBI over SPI 控制 ST7735P3 屏幕（96x160）
**                 2. 提供LCD电源控制（P2.00）与背光PWM调节（pwm20，P1.12）
**                 3. 提供清屏与完整初始化/断电重初始化流程
**                 4. 通过 pm_device_busy 保持显示，防止系统PM空闲时挂起屏幕
**                 5. 通过 SYS_INIT 提前上电，保证显示驱动初始化前屏已供电
*********************************************************************/
#define BLE_LOG_MODULE_ID BLE_LOG_MOD_CTRL

#include "my_comm.h"

LOG_MODULE_REGISTER(my_lcd, LOG_LEVEL_INF);

/* LCD 显示屏设备：chosen 中 zephyr,display 指向 st7735r 节点 */
static const struct device *s_lcd_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
/* LCD 背光 PWM：pwm20 通道0，P1.12 */
static const struct pwm_dt_spec s_lcd_backlight = PWM_DT_SPEC_GET(DT_ALIAS(lcd_backlight));
/* LCD 电源使能引脚：P2.00，高电平有效 */
static const struct gpio_dt_spec s_lcd_pwr_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(lcd_pwr_ctrl), gpios);
/* LCD MIPI-DBI 控制器设备：lcd_mipi_dbi 父节点 */
static const struct device *s_lcd_mipi = DEVICE_DT_GET(DT_PARENT(DT_CHOSEN(zephyr_display)));
/* LCD 复位引脚：P2.04，低电平有效 */
static const struct gpio_dt_spec s_lcd_reset = GPIO_DT_SPEC_GET_BY_IDX(
    DT_PARENT(DT_CHOSEN(zephyr_display)), reset_gpios, 0);
/* LCD SPI 传输配置：复用 zephyr,display 节点的 mipi-mode/频率/CS 信息 */
static const struct mipi_dbi_config s_lcd_dbi_config =
    MIPI_DBI_CONFIG_DT(DT_CHOSEN(zephyr_display),
        SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_HOLD_ON_CS | SPI_LOCK_ON, 0);

/* 背光默认亮度（百分比） */
#define LCD_DEFAULT_BACKLIGHT_PERCENT  50

/* LCD 初始化命令结构体 */
typedef struct
{
    uint8_t cmd;
    const uint8_t *data;
    uint8_t len;
} lcd_init_cmd_t;

/* ST7735P3 初始化参数数组（与厂商提供的初始化序列一致） */
static const uint8_t s_lcd_frmctr1[] = {0x05, 0x3C, 0x3C};
static const uint8_t s_lcd_frmctr2[] = {0x05, 0x3C, 0x3C};
static const uint8_t s_lcd_frmctr3[] = {0x05, 0x3C, 0x3C, 0x05, 0x3C, 0x3C};
static const uint8_t s_lcd_invctr[] = {0x03};
static const uint8_t s_lcd_pwctr1[] = {0x88, 0x08, 0x84};
static const uint8_t s_lcd_pwctr2[] = {0xC4};
static const uint8_t s_lcd_pwctr3[] = {0x0D, 0x00};
static const uint8_t s_lcd_pwctr4[] = {0x8D, 0x2A};
static const uint8_t s_lcd_pwctr5[] = {0x8D, 0xEE};
static const uint8_t s_lcd_vmctr1[] = {0x05};
static const uint8_t s_lcd_gamctrp1[] = {0x13, 0x1E, 0x0F, 0x14, 0x28, 0x21, 0x1A, 0x1E,
                                         0x1E, 0x20, 0x2A, 0x38, 0x00, 0x0C, 0x01, 0x10};
static const uint8_t s_lcd_gamctrn1[] = {0x16, 0x1C, 0x0E, 0x17, 0x30, 0x2B, 0x26, 0x2B,
                                         0x2B, 0x27, 0x2F, 0x3C, 0x00, 0x0E, 0x04, 0x10};
static const uint8_t s_lcd_madctl[] = {0xC8};
static const uint8_t s_lcd_colmod[] = {0x05};
static const uint8_t s_lcd_caset[] = {0x00, 0x12, 0x00, 0x71};
static const uint8_t s_lcd_raset[] = {0x00, 0x01, 0x00, 0xA0};

/* ST7735P3 初始化命令序列 */
static const lcd_init_cmd_t s_lcd_init_seq[] = {
    {0xB1, s_lcd_frmctr1, sizeof(s_lcd_frmctr1)},   /* FRMCTR1 */
    {0xB2, s_lcd_frmctr2, sizeof(s_lcd_frmctr2)},   /* FRMCTR2 */
    {0xB3, s_lcd_frmctr3, sizeof(s_lcd_frmctr3)},   /* FRMCTR3 */
    {0xB4, s_lcd_invctr, sizeof(s_lcd_invctr)},     /* INVCTR: Dot inversion */
    {0xC0, s_lcd_pwctr1, sizeof(s_lcd_pwctr1)},     /* PWCTR1 */
    {0xC1, s_lcd_pwctr2, sizeof(s_lcd_pwctr2)},     /* PWCTR2 */
    {0xC2, s_lcd_pwctr3, sizeof(s_lcd_pwctr3)},     /* PWCTR3 */
    {0xC3, s_lcd_pwctr4, sizeof(s_lcd_pwctr4)},     /* PWCTR4 */
    {0xC4, s_lcd_pwctr5, sizeof(s_lcd_pwctr5)},     /* PWCTR5 */
    {0xC5, s_lcd_vmctr1, sizeof(s_lcd_vmctr1)},     /* VMCTR1: VCOM */
    {0xE0, s_lcd_gamctrp1, sizeof(s_lcd_gamctrp1)}, /* GAMCTRP1 */
    {0xE1, s_lcd_gamctrn1, sizeof(s_lcd_gamctrn1)}, /* GAMCTRN1 */
    {0x36, s_lcd_madctl, sizeof(s_lcd_madctl)},     /* MADCTL: 扫描方向+BGR */
    {0x3A, s_lcd_colmod, sizeof(s_lcd_colmod)},     /* COLMOD: 16bit RGB565 */
    {0x2A, s_lcd_caset, sizeof(s_lcd_caset)},       /* CASET: 列 18~113 */
    {0x2B, s_lcd_raset, sizeof(s_lcd_raset)},       /* RASET: 行 1~160 */
};

/********************************************************************
**函数名称:  my_lcd_pwr_on
**入口参数:  on       ---        true 开启电源，false 关闭电源（输入）
**出口参数:  无
**函数功能:  控制LCD屏电源使能引脚（P2.00）
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int my_lcd_pwr_on(bool on)
{
    if (!gpio_is_ready_dt(&s_lcd_pwr_gpio))
    {
        return -ENODEV;
    }

    return gpio_pin_configure_dt(&s_lcd_pwr_gpio, on ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
}

/********************************************************************
**函数名称:  my_lcd_hold_on
**入口参数:  无
**出口参数:  无
**函数功能:  保持屏幕显示：标记屏为 busy，阻止系统 PM 挂起屏幕，
**           期间系统空闲不会发送 SLPIN，屏幕保持点亮
**返回值:    无
**注意事项:  需在屏幕已上电并初始化后调用；显示结束后调用 my_lcd_hold_off
*********************************************************************/
void my_lcd_hold_on(void)
{
    pm_device_busy_set(s_lcd_dev);
}

/********************************************************************
**函数名称:  my_lcd_hold_off
**入口参数:  无
**出口参数:  无
**函数功能:  解除屏幕保持并断电：清除 busy 标记，关闭 P2.00 屏幕电源
**返回值:    无
**注意事项:  断电后寄存器/GRAM 丢失，下次显示需重新初始化
*********************************************************************/
void my_lcd_hold_off(void)
{
    pm_device_busy_clear(s_lcd_dev);
    my_lcd_pwr_on(false);
}

/********************************************************************
**函数名称:  my_lcd_set_backlight
**入口参数:  level    ---        背光亮度 0~100（输入）
**出口参数:  无
**函数功能:  调节LCD背光亮度（pwm20，P1.12）
**返回值:    0 表示成功，负值表示失败
**注意事项:  占空比按 level 百分比换算 PWM 脉宽
*********************************************************************/
int my_lcd_set_backlight(uint8_t level)
{
    uint32_t pulse_width;
    uint32_t period;

    if (level > 100)
    {
        level = 100;
    }

    if (!pwm_is_ready_dt(&s_lcd_backlight))
    {
        return -ENODEV;
    }

    period = s_lcd_backlight.period;
    pulse_width = (period * level) / 100;

    /* pwm_set_dt 需显式传入周期与脉宽（周期计数单位） */
    return pwm_set_dt(&s_lcd_backlight, period, pulse_width);
}

/********************************************************************
**函数名称:  my_lcd_clear
**入口参数:  color    ---        RGB565 颜色值（输入）
**出口参数:  无
**函数功能:  清屏，将整屏填充为指定颜色
**返回值:    0 表示成功，负值表示失败
**注意事项:  按行分块写入显示控制器，避免使用过大的栈缓冲
*********************************************************************/
int my_lcd_clear(uint16_t color)
{
    struct display_capabilities caps;
    struct display_buffer_descriptor desc;
    uint16_t buf[128];
    uint16_t y;
    uint16_t i;

    if (!device_is_ready(s_lcd_dev))
    {
        return -ENODEV;
    }

    display_get_capabilities(s_lcd_dev, &caps);

    /* 填充一行像素缓冲 */
    for (i = 0; i < ARRAY_SIZE(buf); i++)
    {
        /* RGB565 须以大端字节序存储/传输，否则颜色错乱 */
        buf[i] = sys_cpu_to_be16(color);
    }

    desc.buf_size = sizeof(buf);
    desc.width = caps.x_resolution;   // 使用屏幕实际宽度，避免超出 GRAM 范围
    desc.height = 1;
    desc.pitch = caps.x_resolution;
    desc.frame_incomplete = false;

    /* 按行写入，直至铺满整个屏幕 */
    for (y = 0; y < caps.y_resolution; y++)
    {
        display_write(s_lcd_dev, 0, y, &desc, (void *)buf);
    }

    return 0;
}

/********************************************************************
**函数名称:  my_lcd_init
**入口参数:  无
**出口参数:  无
**函数功能:  初始化LCD显示屏电源、背光与显示控制器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int my_lcd_init(void)
{
    int ret;
    enum pm_device_state state;

    /* 先开启LCD屏电源 */
    ret = my_lcd_pwr_on(true);
    if (ret != 0)
    {
        MY_LOG_ERR("LCD power on failed: %d", ret);
        return ret;
    }

    if (!device_is_ready(s_lcd_dev))
    {
        MY_LOG_ERR("LCD device not ready");
        return -ENODEV;
    }

    /* 唤醒屏幕：仅当屏被系统 PM 挂起（SLPIN）时需 RESUME（发 SLPOUT），
     * 屏已唤醒则跳过，避免无谓的 120ms 等待 */
    pm_device_state_get(s_lcd_dev, &state);
    if (state == PM_DEVICE_STATE_SUSPENDED)
    {
        ret = pm_device_action_run(s_lcd_dev, PM_DEVICE_ACTION_RESUME);
        if (ret != 0)
        {
            MY_LOG_WRN("LCD resume failed: %d", ret);
        }
    }

    /* 取消 blanking，点亮屏幕 */
    ret = display_blanking_off(s_lcd_dev);
    if (ret != 0)
    {
        MY_LOG_ERR("LCD blanking off failed: %d", ret);
        return ret;
    }

    /* 上电后 GRAM 为随机值，主动清屏覆盖，避免花屏 */
    ret = my_lcd_clear(0x07E0);   /* 清屏为绿色，可按产品需求改为其他颜色 */
    if (ret != 0)
    {
        MY_LOG_ERR("LCD clear failed: %d", ret);
    }

    /* 保持显示：阻止系统空闲时 SLPIN 挂起屏幕，否则屏幕会立即变白屏；
     * 需要关闭屏幕时调用 my_lcd_hold_off() */
    my_lcd_hold_on();

    MY_LOG_INF("LCD module initialized");
    return 0;
}

/********************************************************************
**函数名称:  my_lcd_dbi_write
**入口参数:  cmd      ---        LCD 指令码（输入）
            data     ---        指令参数（输入）
            len      ---        参数长度（输入）
**出口参数:  无
**函数功能:  通过 MIPI-DBI 发送指令，并释放 SPI 总线锁
**返回值:    0 表示成功，负值表示失败
**注意事项:  配置使用 SPI_LOCK_ON，发送后必须 mipi_dbi_release 释放总线，
**           否则后续 SPI 传输（如 display_write）会阻塞等待总线锁
*********************************************************************/
static int my_lcd_dbi_write(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    int ret;

    ret = mipi_dbi_command_write(s_lcd_mipi, &s_lcd_dbi_config, cmd, data, len);
    mipi_dbi_release(s_lcd_mipi, &s_lcd_dbi_config);

    return ret;
}

/********************************************************************
**函数名称:  my_lcd_reinit
**入口参数:  无
**出口参数:  无
**函数功能:  屏幕断电后重新上电并完成完整初始化（复位+初始化序列+显示开启）
**返回值:    0 表示成功，负值表示失败
**注意事项:  断电期间芯片寄存器与 GRAM 全部丢失，重新上电后必须重走完整初始化；
**           初始化完成后需由调用方重新写入显示内容
*********************************************************************/
int my_lcd_reinit(void)
{
    int ret;
    uint32_t i;

    /* 1. 开启屏电源并等待电源稳定 */
    ret = my_lcd_pwr_on(true);
    if (ret != 0)
    {
        MY_LOG_ERR("LCD power on failed: %d", ret);
        return ret;
    }
    k_msleep(50);

    /* 2. 复位脉冲：等待 MTP 出厂参数加载 */
    ret = gpio_pin_configure_dt(&s_lcd_reset, GPIO_OUTPUT_INACTIVE);
    if (ret != 0)
    {
        MY_LOG_ERR("LCD reset gpio config failed: %d", ret);
        return ret;
    }
    gpio_pin_set_dt(&s_lcd_reset, 1);
    k_msleep(1);
    gpio_pin_set_dt(&s_lcd_reset, 0);
    k_msleep(120);

    /* 3. 退出睡眠，触发 MTP 加载并启动 DC/DC */
    ret = my_lcd_dbi_write(0x11, NULL, 0);
    if (ret != 0)
    {
        MY_LOG_ERR("LCD sleep out failed: %d", ret);
        return ret;
    }
    k_msleep(120);

    /* 4. 逐条发送面板/系统初始化命令（厂商参数） */
    for (i = 0; i < ARRAY_SIZE(s_lcd_init_seq); i++)
    {
        ret = my_lcd_dbi_write(s_lcd_init_seq[i].cmd, s_lcd_init_seq[i].data,
                               s_lcd_init_seq[i].len);
        if (ret != 0)
        {
            MY_LOG_ERR("LCD init cmd 0x%02X failed: %d", (unsigned int)s_lcd_init_seq[i].cmd, ret);
            return ret;
        }
    }

    /* 5. 显示开启 */
    ret = my_lcd_dbi_write(0x29, NULL, 0);
    if (ret != 0)
    {
        MY_LOG_ERR("LCD display on failed: %d", ret);
        return ret;
    }

    /* 6. 清屏为黑色，覆盖上电后 GRAM 随机值，避免花屏/白屏闪烁 */
    ret = my_lcd_clear(0x0000);
    if (ret != 0)
    {
        MY_LOG_ERR("LCD clear black failed: %d", ret);
        return ret;
    }

    /* 7. 恢复背光亮度 */
    my_lcd_set_backlight(LCD_DEFAULT_BACKLIGHT_PERCENT);

    MY_LOG_INF("LCD reinitialized");
    return 0;
}

/********************************************************************
**函数名称:  lcd_pwr_early_init
**入口参数:  无
**出口参数:  无
**函数功能:  在 ST7735R 显示驱动初始化之前打开 LCD 电源（P2.00），
**           确保屏上电后再执行初始化命令序列，避免命令无效
**返回值:    0 表示成功，负值表示失败
**注意事项:  通过 SYS_INIT 注册到 POST_KERNEL 阶段，优先级 80 早于
**           CONFIG_DISPLAY_INIT_PRIORITY（显示驱动初始化）
*********************************************************************/
static int lcd_pwr_early_init(void)
{
    int ret;

    ret = my_lcd_pwr_on(true);
    if (ret != 0)
    {
        MY_LOG_ERR("LCD power early init failed: %d", ret);
        return ret;
    }

    /* 等待电源稳定后再执行显示驱动初始化 */
    k_msleep(50);

    /* 设置默认背光亮度，确保上电即亮（pwm20 初始化早于本阶段，可直接使用） */
    ret = my_lcd_set_backlight(LCD_DEFAULT_BACKLIGHT_PERCENT);
    if (ret != 0)
    {
        MY_LOG_WRN("LCD backlight set failed: %d", ret);
    }

    return 0;
}

/* 提前打开 LCD 电源，确保屏在显示驱动初始化前已上电 */
SYS_INIT(lcd_pwr_early_init, POST_KERNEL, 80);
