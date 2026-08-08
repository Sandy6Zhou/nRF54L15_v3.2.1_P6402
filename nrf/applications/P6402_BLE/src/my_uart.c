/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_uart.c
**文件描述:        UART 抽象层实现
**当前版本:        V1.0
**作    者:        Felix Tang (tangchaofa@jimiiot.com)
**完成日期:        2026.08.03
*********************************************************************/

/* 必须在包含 my_comm.h 之前定义 BLE_LOG_MODULE_ID，避免与 my_ble_log.h 中的默认定义冲突 */
#define BLE_LOG_MODULE_ID BLE_LOG_MOD_UART

#include "my_comm.h"

LOG_MODULE_REGISTER(my_uart, LOG_LEVEL_INF);

/********************************************************************
**函数名称:  my_uart_init
**入口参数:  config   ---        UART 初始化配置结构体指针
**出口参数:  无
**函数功能:  通用 UART 异步初始化，包含以下步骤：
**           1. 参数有效性检查
**           2. UART 设备就绪检查
**           3. 环形缓冲区初始化
**           4. 发送完成信号量初始化（初始值 1，表示空闲）
**           5. UART 异步回调注册
**返回值:    0 表示成功
**           -EINVAL --- 参数无效（config 为 NULL）
**           -ENODEV --- UART 设备未就绪
**           其他 --- uart_callback_set 返回的错误码
*********************************************************************/
int my_uart_init(my_uart_config_t *config)
{
    int err;

    if (!device_is_ready(config->dev))
    {
        MY_LOG_ERR("UART[%s] device is not ready: %d", config->dev->name, err);
        return -ENODEV;
    }

    /* 初始化串口接收循环缓冲区 */
    my_rb_init(config->rb, config->rb_buf, config->rb_size);

    /* 初始值为1(表示UART空闲) */
    k_sem_init(config->tx_done_sem, 1, 1);

    /* 设置 UART 异步回调 */
    err = uart_callback_set(config->dev, config->cb, config->cb_user_data);
    if (err != 0)
    {
        MY_LOG_ERR("UART[%s] callback set failed: %d", config->dev->name, err);
        return err;
    }

    return 0;
}
