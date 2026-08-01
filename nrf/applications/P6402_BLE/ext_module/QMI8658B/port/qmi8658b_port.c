/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        qmi8658b_port.c
**文件描述:        QMI8658B Zephyr I2C 和 GPIO 端口层实现文件
**当前版本:        V1.0
**作    者:        周森达 (zhousenda@jimiiot.com)
**完成日期:        2026.07.30
*********************************************************************
** 功能描述:        适配 Zephyr I2C 总线、INT1 GPIO 和延时接口供 QMI8658B 驱动使用
*********************************************************************/

#include "qmi8658b_port.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#define QMI8658B_I2C_ADDR                0x6BU

static const struct device *s_i2c_dev = DEVICE_DT_GET(DT_ALIAS(gsensor_i2c));
static const struct gpio_dt_spec s_int_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(gsensor_int), gpios);
static struct gpio_callback s_int_gpio_callback;
static void (*s_int_callback)(void);
static bool s_int_callback_registered;
static bool s_port_initialized;

/********************************************************************
**函数名称:  qmi8658b_port_read
**入口参数:  context  ---        保留上下文（输入）
**           reg_addr ---        寄存器地址（输入）
**           data     ---        数据缓冲区（输出）
**           len      ---        读取长度（输入）
**出口参数:  data     ---        寄存器数据
**函数功能:  通过 Zephyr I2C 读取芯片寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int qmi8658b_port_read(void *context, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    ARG_UNUSED(context);

    return i2c_burst_read(s_i2c_dev, QMI8658B_I2C_ADDR, reg_addr, data, len);
}

/********************************************************************
**函数名称:  qmi8658b_port_write
**入口参数:  context  ---        保留上下文（输入）
**           reg_addr ---        寄存器地址（输入）
**           data     ---        写入数据（输入）
**           len      ---        写入长度（输入）
**出口参数:  无
**函数功能:  通过 Zephyr I2C 写入芯片寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int qmi8658b_port_write(void *context, uint8_t reg_addr, const uint8_t *data, uint16_t len)
{
    ARG_UNUSED(context);

    return i2c_burst_write(s_i2c_dev, QMI8658B_I2C_ADDR, reg_addr, data, len);
}

/********************************************************************
**函数名称:  qmi8658b_port_delay_ms
**入口参数:  delay_ms ---        延时毫秒数（输入）
**出口参数:  无
**函数功能:  提供可调度的毫秒级延时
**返回值:    无
*********************************************************************/
static void qmi8658b_port_delay_ms(uint32_t delay_ms)
{
    k_msleep(delay_ms);
}

static const qmi8658b_bus_t s_bus =
{
    .context = NULL,
    .read = qmi8658b_port_read,
    .write = qmi8658b_port_write,
    .delay_ms = qmi8658b_port_delay_ms,
};

/********************************************************************
**函数名称:  qmi8658b_port_gpio_isr
**入口参数:  port     ---        GPIO 控制器（输入）
**           callback ---        GPIO 回调对象（输入）
**           pins     ---        触发引脚位图（输入）
**出口参数:  无
**函数功能:  转发 INT1 GPIO 中断通知
**返回值:    无
*********************************************************************/
static void qmi8658b_port_gpio_isr(const struct device *port, struct gpio_callback *callback, gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(callback);

    if (((pins & BIT(s_int_gpio.pin)) != 0U) && (s_int_callback != NULL))  // 仅响应已配置的 INT1 引脚上升沿
    {
        s_int_callback();
    }
}

/********************************************************************
**函数名称:  qmi8658b_port_init
**入口参数:  无
**出口参数:  无
**函数功能:  检查 QMI8658B I2C 和 INT1 硬件资源
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_port_init(void)
{
    int ret;

    if (!device_is_ready(s_i2c_dev) || !gpio_is_ready_dt(&s_int_gpio))
    {
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&s_int_gpio, GPIO_INPUT);
    if (ret != 0)
    {
        return ret;
    }

    s_port_initialized = true;
    return 0;
}

/********************************************************************
**函数名称:  qmi8658b_port_get_bus
**入口参数:  无
**出口参数:  无
**函数功能:  获取已适配的总线回调集合
**返回值:    成功返回总线回调指针，失败返回 NULL
*********************************************************************/
const qmi8658b_bus_t *qmi8658b_port_get_bus(void)
{
    if (!s_port_initialized)
    {
        return NULL;
    }

    return &s_bus;
}

/********************************************************************
**函数名称:  qmi8658b_port_register_int_callback
**入口参数:  callback ---        通用中断回调（输入）
**出口参数:  无
**函数功能:  注册 INT1 GPIO 上升沿回调
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_port_register_int_callback(void (*callback)(void))
{
    int ret;

    if (!s_port_initialized)
    {
        return -EACCES;
    }

    s_int_callback = callback;
    if (!s_int_callback_registered)
    {
        gpio_init_callback(&s_int_gpio_callback, qmi8658b_port_gpio_isr, BIT(s_int_gpio.pin));
        ret = gpio_add_callback(s_int_gpio.port, &s_int_gpio_callback);
        if (ret != 0)
        {
            return ret;
        }

        s_int_callback_registered = true;
    }

    return gpio_pin_interrupt_configure_dt(&s_int_gpio, GPIO_INT_EDGE_RISING);
}
