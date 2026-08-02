#define BLE_LOG_MODULE_ID BLE_LOG_MOD_CTRL

#include "my_comm.h"
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/pwm.h>

LOG_MODULE_REGISTER(my_lcd, LOG_LEVEL_INF);

/* LCD 显示屏设备：chosen 中 zephyr,display 指向 st7735r 节点 */
static const struct device *s_lcd_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
/* LCD 背光 PWM：pwm21 通道0，P1.11 */
static const struct pwm_dt_spec s_lcd_backlight = PWM_DT_SPEC_GET(DT_ALIAS(lcd_backlight));
/* LCD 电源使能引脚：P2.00，高电平有效 */
static const struct gpio_dt_spec s_lcd_pwr_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(lcd_pwr_ctrl), gpios);

/* 背光默认亮度（百分比） */
#define LCD_DEFAULT_BACKLIGHT_PERCENT  50

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
**函数名称:  my_lcd_set_backlight
**入口参数:  level    ---        背光亮度 0~100（输入）
**出口参数:  无
**函数功能:  调节LCD背光亮度（pwm21，P1.11）
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

    if (!device_is_ready(s_lcd_dev))
    {
        return -ENODEV;
    }

    display_get_capabilities(s_lcd_dev, &caps);

    /* 填充一行像素缓冲 */
    for (size_t i = 0; i < ARRAY_SIZE(buf); i++)
    {
        buf[i] = color;
    }

    desc.buf_size = sizeof(buf);
    desc.width = ARRAY_SIZE(buf);
    desc.height = 1;
    desc.pitch = ARRAY_SIZE(buf);
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

    /* 取消 blanking，点亮屏幕 */
    ret = display_blanking_off(s_lcd_dev);
    if (ret != 0)
    {
        MY_LOG_ERR("LCD blanking off failed: %d", ret);
        return ret;
    }

    /* 设置默认背光亮度 */
    ret = my_lcd_set_backlight(LCD_DEFAULT_BACKLIGHT_PERCENT);
    if (ret != 0)
    {
        MY_LOG_WRN("LCD backlight set failed: %d", ret);
    }

    MY_LOG_INF("LCD module initialized");
    return 0;
}
