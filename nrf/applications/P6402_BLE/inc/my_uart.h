/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_uart.h
**文件描述:        UART 抽象层头文件
**当前版本:        V1.0
**作    者:        Felix Tang (tangchaofa@jimiiot.com)
**完成日期:        2026.08.03
*********************************************************************
** 功能描述:        1. 提供 UART 异步初始化抽象接口，供各业务模块复用
**                 2. 通过 my_uart_config_t 传入差异化参数
*********************************************************************/
#ifndef _MY_UART_H_
#define _MY_UART_H_

#include "my_ring_buf.h"

/* UART 异步事件回调类型 */
typedef void (*my_uart_cb_t)(const struct device *dev, struct uart_event *evt, void *user_data);

/* UART 初始化配置结构体 */
typedef struct
{
    const struct device *dev;       /* UART 设备指针 */
    my_uart_cb_t cb;                /* 异步事件回调 */
    void *cb_user_data;             /* 回调用户数据 */
    ring_buffer_t *rb;              /* 环形缓冲区实例指针 */
    uint8_t *rb_buf;                /* 环形缓冲区内存 */
    uint32_t rb_size;               /* 环形缓冲区大小 */
    struct k_sem *tx_done_sem;      /* 发送完成信号量指针 */
} my_uart_config_t;

/********************************************************************
**函数名称:  my_uart_init
**入口参数:  config   ---        UART 初始化配置结构体指针
**出口参数:  无
**函数功能:  通用 UART 异步初始化，包含设备就绪检查、环形缓冲区初始化、
**           发送完成信号量初始化、异步回调注册
**返回值:    0 表示成功，其他表示失败
*********************************************************************/
int my_uart_init(my_uart_config_t *config);

#endif /* _MY_UART_H_ */
