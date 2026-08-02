/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_wifi_uart.c
**文件描述:        WIFI串口模块实现文件
**当前版本:        V1.0
**作    者:       周森达 (zhousenda@jimiiot.com)
**完成日期:        2026.07.20
*********************************************************************
** 功能描述:        1. 实现 UART20 WIFI串口异步收发
**                 2. 使用独立线程处理WIFI串口接收数据
**                 3. 接入统一 PM 框架，支持串口挂起与恢复
**                 4. 支持WIFI唤醒引脚(P0.02)中断恢复UART链路
*********************************************************************/

/* 必须在包含 my_comm.h 之前定义 BLE_LOG_MODULE_ID，避免与 my_ble_log.h 中的默认定义冲突 */
#define BLE_LOG_MODULE_ID BLE_LOG_MOD_WIFI_UART

#include "my_comm.h"

LOG_MODULE_REGISTER(my_wifi_uart, LOG_LEVEL_INF);

#define WIFI_UART_BUF_SIZE               256
#define WIFI_UART_TX_BUFFER_SIZE         256
#define WIFI_UART_RB_SIZE                512
#define WIFI_UART_TX_WAIT_MS             200
// WIFI串口发送唤醒窗口：预留休眠/唤醒机制，窗口内认为链路仍处于活跃态
#define WIFI_UART_SEND_WAKEUP_WINDOW_MS  2500
// WIFI串口空闲超时时间：预留后续挂起机制，当前调试模式下不会真正执行挂起
#define WIFI_UART_IDLE_TIMEOUT_MS        3000
// 当前WIFI串口无独立唤醒中断脚，故默认保持调试常开模式
#define WIFI_UART_DEBUG_ENABLE           1

#define WIFI_UART_NODE DT_ALIAS(wifi_uart)
static const struct device *s_wifi_uart_dev = DEVICE_DT_GET(WIFI_UART_NODE);

/* WIFI唤醒引脚：P0.02，外部上拉，WIFI拉低唤醒Nordic */
static const struct gpio_dt_spec s_wifi_wake_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(wifi_wake_ctrl), gpios);
static struct gpio_callback s_wifi_wake_cb_data;

/* WIFI电源控制引脚：P2.08，高电平有效 */
static const struct gpio_dt_spec s_wifi_pwr_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(wifi_pwr_ctrl), gpios);

typedef struct
{
    struct k_timer idle_timer;               // 空闲定时器：预留给后续WIFI串口挂起场景
    int64_t send_wakeup_expire_timestamp_ms; // 发送唤醒窗口到期时间戳（毫秒）
    bool active;                             // 当前 UART RX 是否处于使能状态
    bool tx_busy;                            // 当前是否存在正在进行的异步发送
    volatile bool wakeup_pending;            // WIFI唤醒消息是否已投递，避免GPIO抖动重复塞消息
} wifi_uart_ctx_t;

static int wifi_uart_pm_init(void);
static int wifi_uart_pm_suspend(void);
static int wifi_uart_pm_resume(void);

static const pm_device_ops_t s_wifi_uart_pm_ops =
{
    .init = wifi_uart_pm_init,
    .suspend = wifi_uart_pm_suspend,
    .resume = wifi_uart_pm_resume,
};

static struct k_sem s_wifi_uart_tx_done_sem;
static wifi_uart_ctx_t s_wifi_uart_ctx = { 0 };
static wifi_uart_rx_handler_t s_wifi_uart_rx_handler = NULL;
static uint8_t s_wifi_uart_rx_buf_1[WIFI_UART_BUF_SIZE];
static uint8_t s_wifi_uart_rx_buf_2[WIFI_UART_BUF_SIZE];
static uint8_t *s_wifi_uart_next_buf = s_wifi_uart_rx_buf_2;
static uint8_t s_wifi_uart_rb_buf[WIFI_UART_RB_SIZE];
static ring_buffer_t s_wifi_uart_rb;

K_MSGQ_DEFINE(my_wifi_uart_msgq, sizeof(msg_t), 10, 4);
K_THREAD_STACK_DEFINE(my_wifi_uart_task_stack, MY_WIFI_TASK_STACK_SIZE);
static struct k_thread s_my_wifi_uart_task_data;

/********************************************************************
**函数名称:  wifi_uart_idle_timer_handler
**入口参数:  timer    ---        定时器句柄
**出口参数:  无
**函数功能:  磁吸串口空闲定时器回调，通知线程执行挂起判断
**返回值:    无
*********************************************************************/
static void wifi_uart_idle_timer_handler(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    my_send_msg(MOD_WIFI_UART, MOD_WIFI_UART, MY_MSG_WIFI_UART_IDLE);
}

/********************************************************************
**函数名称:  wifi_wake_pin_isr
**入口参数:  port     ---        GPIO端口
**           cb       ---        GPIO回调结构体
**           pins     ---        触发中断的引脚位掩码
**出口参数:  无
**函数功能:  WIFI唤醒引脚(P0.02)中断回调，WIFI模块发数据前拉低此引脚
**           唤醒Nordic，本回调通知WIFI串口线程恢复UART接收
**返 回 值:  无
*********************************************************************/
static void wifi_wake_pin_isr(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

#if WIFI_UART_DEBUG_ENABLE
    return;
#endif

    // GPIO抖动或短时间重复触发时，只保留一个待处理唤醒消息
    if (!s_wifi_uart_ctx.wakeup_pending)
    {
        s_wifi_uart_ctx.wakeup_pending = true;
        my_send_msg(MOD_WIFI_UART, MOD_WIFI_UART, MY_MSG_WIFI_UART_WAKEUP);
    }
}

/********************************************************************
**函数名称:  my_wifi_pwr_on
**入口参数:  on       ---        true 开启，false 关闭
**出口参数:  无
**函数功能:  控制WIFI模组供电（P2.08）
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int my_wifi_pwr_on(bool on)
{
    if (!gpio_is_ready_dt(&s_wifi_pwr_gpio))
    {
        return -ENODEV;
    }

    return gpio_pin_configure_dt(&s_wifi_pwr_gpio, on ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
}

/********************************************************************
**函数名称:  wifi_uart_pm_init
**入口参数:  无
**出口参数:  无
**函数功能:  磁吸串口电源管理初始化，重置上下文状态
**返回值:    0 --- 始终成功
*********************************************************************/
static int wifi_uart_pm_init(void)
{
    s_wifi_uart_ctx.active = false;
    s_wifi_uart_ctx.tx_busy = false;
    s_wifi_uart_ctx.send_wakeup_expire_timestamp_ms = 0;
    k_timer_init(&s_wifi_uart_ctx.idle_timer, wifi_uart_idle_timer_handler, NULL);

    return 0;
}

/********************************************************************
**函数名称:  wifi_uart_pm_suspend
**入口参数:  无
**出口参数:  无
**函数功能:  磁吸串口挂起处理，关闭 RX 接收以节省功耗
**返回值:    0 --- 挂起成功
**           其他 --- uart_rx_disable 返回错误码
*********************************************************************/
static int wifi_uart_pm_suspend(void)
{
    int ret;

#if WIFI_UART_DEBUG_ENABLE
    MY_LOG_INF("WIFI UART suspend disabled by macro");
    return 0;
#endif

    s_wifi_uart_ctx.active = false;

    ret = uart_rx_disable(s_wifi_uart_dev);
    if ((ret != 0) && (ret != -EFAULT) && (ret != -EINVAL) && (ret != -EBUSY))
    {
        MY_LOG_ERR("WIFI UART RX disable failed: %d", ret);
        s_wifi_uart_ctx.active = true;
        return ret;
    }

    MY_LOG_INF("WIFI UART suspended");
    return 0;
}

/********************************************************************
**函数名称:  wifi_uart_pm_resume
**入口参数:  无
**出口参数:  无
**函数功能:  磁吸串口恢复处理，重新启用 RX 接收
**返回值:    0 --- 恢复成功
**           其他 --- uart_rx_enable 返回错误码
*********************************************************************/
static int wifi_uart_pm_resume(void)
{
    int ret;

    // 恢复时重新打开 UART 异步接收，后续接收数据会继续通过 RX 事件进入线程处理流程
    s_wifi_uart_next_buf = s_wifi_uart_rx_buf_2;
    ret = uart_rx_enable(s_wifi_uart_dev, s_wifi_uart_rx_buf_1, WIFI_UART_BUF_SIZE, 10 * USEC_PER_MSEC);
    if ((ret != 0) && (ret != -EBUSY))
    {
        MY_LOG_ERR("Failed to enable WIFI UART RX in resume: %d", ret);
        return ret;
    }

    s_wifi_uart_ctx.active = true;
    MY_LOG_INF("WIFI UART resumed");
    return 0;
}

/********************************************************************
**函数名称:  wifi_uart_ensure_active
**入口参数:  无
**出口参数:  无
**函数功能:  确保磁吸串口处于活跃态，若已挂起则执行恢复
**返回值:    0 --- 已活跃或恢复成功
**           其他 --- 错误码
*********************************************************************/
static int wifi_uart_ensure_active(void)
{
    int ret;

    if (!s_wifi_uart_ctx.active)
    {
        // 发送前主动恢复 PM，避免 UART 处于挂起态时直接下发发送请求
        ret = my_pm_device_resume(MY_PM_DEV_WIFI_UART);
        if (ret < 0)
        {
            MY_LOG_ERR("Failed to resume WIFI UART: %d", ret);
            return ret;
        }
    }

    return 0;
}

/********************************************************************
**函数名称:  wifi_uart_can_suspend
**入口参数:  无
**出口参数:  无
**函数功能:  检查磁吸串口是否满足挂起条件
**返回值:    true --- 可以挂起
**           false --- 不允许挂起
*********************************************************************/
static bool wifi_uart_can_suspend(void)
{
    if (my_rb_get_used_size(&s_wifi_uart_rb) > 0)
    {
        return false;
    }

    if (s_wifi_uart_ctx.tx_busy)
    {
        return false;
    }

    return true;
}

/********************************************************************
**函数名称:  wifi_uart_activity_kick
**入口参数:  无
**出口参数:  无
**函数功能:  刷新磁吸串口活跃定时器，延迟挂起以维持通信
**返回值:    无
*********************************************************************/
static void wifi_uart_activity_kick(void)
{
#if WIFI_UART_DEBUG_ENABLE
    return;
#endif

    if (!s_wifi_uart_ctx.active)
    {
        return;
    }

    k_timer_start(&s_wifi_uart_ctx.idle_timer, K_MSEC(WIFI_UART_IDLE_TIMEOUT_MS), K_NO_WAIT);
}

/********************************************************************
**函数名称:  wifi_uart_send_wakeup_window_kick
**入口参数:  无
**出口参数:  无
**函数功能:  刷新磁吸串口发送唤醒窗口
**返回值:    无
**注意事项:  当前未接入实际唤醒脚，该时间窗仅作为后续休眠机制的预留状态
*********************************************************************/
static void wifi_uart_send_wakeup_window_kick(void)
{
    s_wifi_uart_ctx.send_wakeup_expire_timestamp_ms =
        k_uptime_get() + WIFI_UART_SEND_WAKEUP_WINDOW_MS;
}

/********************************************************************
**函数名称:  wifi_uart_need_send_wakeup
**入口参数:  无
**出口参数:  无
**函数功能:  判断当前是否已超出磁吸串口发送唤醒窗口
**返回值:    true  --- 已超时，后续若接入唤醒机制则需先执行唤醒
**           false --- 未超时，链路仍认为处于活跃窗口内
**注意事项:  当前函数仅用于预留状态判断，不会触发实际唤醒动作
*********************************************************************/
static bool wifi_uart_need_send_wakeup(void)
{
    int64_t current_timestamp_ms;

    current_timestamp_ms = k_uptime_get();

    return (current_timestamp_ms >= s_wifi_uart_ctx.send_wakeup_expire_timestamp_ms);
}

/********************************************************************
**函数名称:  wifi_uart_test_rx_handler
**入口参数:  data     ---        接收到的数据指针
**           len      ---        数据长度
**出口参数:  无
**函数功能:  默认测试接收回调，打印接收内容并支持简单PING/PONG联调
**返回值:    无
**注意事项:  当前在磁吸串口线程上下文中执行，可安全调用发送接口
*********************************************************************/
static void wifi_uart_test_rx_handler(uint8_t *data, uint16_t len)
{
    static const uint8_t s_ping_cmd[] = "PING";
    static const uint8_t s_ping_rsp[] = "PONG\r\n";

    if ((data == NULL) || (len == 0))
    {
        return;
    }

    LOG_HEXDUMP_INF(data, len, "wifi_uart_rx_test");

    if ((len == sizeof(s_ping_cmd) - 1U) &&
        (memcmp(data, s_ping_cmd, sizeof(s_ping_cmd) - 1U) == 0))
    {
        MY_LOG_INF("WIFI UART test rx matched PING, reply PONG");
        my_wifi_uart_send(s_ping_rsp, sizeof(s_ping_rsp) - 1U);
    }
}

/********************************************************************
**函数名称:  my_wifi_uart_handle_recv
**入口参数:  data     ---        接收到的数据指针
**           len      ---        数据长度
**出口参数:  无
**函数功能:  处理磁吸串口接收到的数据，并转发到已注册接收回调
**返回值:    无
*********************************************************************/
static void my_wifi_uart_handle_recv(uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0))
    {
        return;
    }

    if (s_wifi_uart_rx_handler != NULL)
    {
        s_wifi_uart_rx_handler(data, len);
    }
}

/********************************************************************
**函数名称:  wifi_uart_cb
**入口参数:  dev       ---        UART 设备句柄
**           evt       ---        UART 事件结构体
**           user_data ---        用户自定义数据
**出口参数:  无
**函数功能:  磁吸串口异步事件回调
**返回值:    无
**注意事项:  中断上下文只做缓冲搬运和投递消息，不在此处做复杂业务处理
*********************************************************************/
static void wifi_uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    switch (evt->type)
    {
        case UART_TX_DONE:
            my_send_msg(MOD_WIFI_UART, MOD_WIFI_UART, MY_MSG_WIFI_UART_TX_DONE);
            // 传输完成，释放信号量
            k_sem_give(&s_wifi_uart_tx_done_sem);
            break;

        case UART_RX_RDY:
            // 将中断回调接收到的数据搬运到环形缓冲区，在线程中统一处理
            my_rb_write(&s_wifi_uart_rb, &evt->data.rx.buf[evt->data.rx.offset], evt->data.rx.len);
            // 通知wifi_uart线程读取循环缓冲区数据
            my_send_msg(MOD_WIFI_UART, MOD_WIFI_UART, MY_MSG_WIFI_UART_REV);
            break;

        case UART_RX_BUF_REQUEST:
            // 填充下一个接收缓冲区
            uart_rx_buf_rsp(dev, s_wifi_uart_next_buf, WIFI_UART_BUF_SIZE);
            break;

        case UART_RX_BUF_RELEASED:
            // 双缓冲模式下保存刚释放的缓冲区，供下一次 RX_BUF_REQUEST 继续复用
            s_wifi_uart_next_buf = evt->data.rx_buf.buf;
            break;

        case UART_RX_DISABLED:
            if (s_wifi_uart_ctx.active)
            {
                // 非主动挂起场景下若驱动关闭了 RX，则立即补开，保持接收链路持续有效
                s_wifi_uart_next_buf = s_wifi_uart_rx_buf_2;
                uart_rx_enable(dev, s_wifi_uart_rx_buf_1, WIFI_UART_BUF_SIZE, 10 * USEC_PER_MSEC);
            }
            break;

        case UART_TX_ABORTED:
            // MY_LOG_WRN("WIFI UART TX aborted");
            my_send_msg(MOD_WIFI_UART, MOD_WIFI_UART, MY_MSG_WIFI_UART_TX_ABORTED);
            k_sem_give(&s_wifi_uart_tx_done_sem);
            break;

        default:
            break;
    }
}

/********************************************************************
**函数名称:  my_wifi_uart_send
**入口参数:  data     ---        待发送数据指针
**           len      ---        数据长度
**出口参数:  无
**函数功能:  通过磁吸串口发送指定长度数据
**返回值:    0 表示成功，其他表示失败
*********************************************************************/
int my_wifi_uart_send(const uint8_t *data, uint16_t len)
{
    int ret = 0;
    bool need_send_wakeup = false;
    static uint8_t s_wifi_uart_tx_buf[WIFI_UART_TX_BUFFER_SIZE] = {0};

    if ((data == NULL) || (len == 0))
    {
        return -EINVAL;
    }

    if (len > WIFI_UART_TX_BUFFER_SIZE)
    {
        MY_LOG_ERR("WIFI UART TX data too large: %d", len);
        return -EINVAL;
    }

    ret = wifi_uart_ensure_active();
    if (ret < 0)
    {
        return ret;
    }

    // 发送前先刷新空闲计时，避免未来启用挂起时在等待发送完成期间被误判为空闲
    wifi_uart_activity_kick();

    // 等待上一次传输完成，增加等待超时避免异常状态下永久阻塞
    ret = k_sem_take(&s_wifi_uart_tx_done_sem, K_MSEC(WIFI_UART_TX_WAIT_MS));
    if (ret != 0)
    {
        return ret;
    }

    s_wifi_uart_ctx.tx_busy = true;

    // 当前无独立唤醒脚，仅记录发送窗口状态，便于后续接入休眠/唤醒方案时直接复用
    need_send_wakeup = wifi_uart_need_send_wakeup();
    ARG_UNUSED(need_send_wakeup);

    // 只要本次进入实际发送流程，就刷新一次发送唤醒窗口
    wifi_uart_send_wakeup_window_kick();

    // UART 异步发送期间上层入参内容可能变化，先拷贝到静态缓冲区再交给驱动
    memcpy(s_wifi_uart_tx_buf, data, len);

    ret = uart_tx(s_wifi_uart_dev, s_wifi_uart_tx_buf, len, SYS_FOREVER_MS);
    if (ret != 0)
    {
        s_wifi_uart_ctx.tx_busy = false;
        k_sem_give(&s_wifi_uart_tx_done_sem);
        return ret;
    }

    return 0;
}

/********************************************************************
**函数名称:  my_wifi_uart_register_rx_handler
**入口参数:  handler  ---        接收回调函数指针
**出口参数:  无
**函数功能:  注册磁吸串口接收数据处理回调
**返回值:    无
*********************************************************************/
void my_wifi_uart_register_rx_handler(wifi_uart_rx_handler_t handler)
{
    s_wifi_uart_rx_handler = handler;
}

/********************************************************************
**函数名称:  my_wifi_uart_task
**入口参数:  p1       ---        保留参数
**           p2       ---        保留参数
**           p3       ---        保留参数
**出口参数:  无
**函数功能:  磁吸串口主线程，处理 UART 事件消息
**返回值:    无
*********************************************************************/
static void my_wifi_uart_task(void *p1, void *p2, void *p3)
{
    msg_t msg;
    int len;
    static uint8_t read_buf[128];

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    MY_LOG_INF("WIFI UART thread started");

    for (;;)
    {
        my_recv_msg(&my_wifi_uart_msgq, (void *)&msg, sizeof(msg_t), K_FOREVER);

        switch (msg.msgID)
        {
            case MY_MSG_WIFI_UART_TX_DONE:
            case MY_MSG_WIFI_UART_TX_ABORTED:
                // 发送完成后仅清 busy 状态，不额外延长空闲计时窗口
                s_wifi_uart_ctx.tx_busy = false;
                break;

            case MY_MSG_WIFI_UART_IDLE:
                if (s_wifi_uart_ctx.active && wifi_uart_can_suspend())
                {
                    my_pm_device_suspend(MY_PM_DEV_WIFI_UART);
                }
                break;

            case MY_MSG_WIFI_UART_WAKEUP:
                // WIFI唤醒事件：恢复UART接收链路，并清除待处理标志
                s_wifi_uart_ctx.wakeup_pending = false;
                my_pm_device_resume(MY_PM_DEV_WIFI_UART);
                break;

            case MY_MSG_WIFI_UART_REV:
                // 收到数据说明链路处于活跃通信阶段，先刷新空闲窗口和发送唤醒窗口
                wifi_uart_activity_kick();
                wifi_uart_send_wakeup_window_kick();

                while (1)
                {
                    memset(read_buf, 0, sizeof(read_buf));

                    // 在线程上下文分批取出接收数据，交给注册回调处理
                    len = my_rb_read(&s_wifi_uart_rb, read_buf, sizeof(read_buf));
                    if (len <= 0)
                    {
                        break;
                    }

                    my_wifi_uart_handle_recv(read_buf, (uint16_t)len);
                }
                break;

            case MY_MSG_WIFI_UART_SEND:
                if ((msg.pData != NULL) && (msg.DataLen > 0))
                {
                    // 预留消息方式发送入口，便于后续其他模块通过消息队列投递串口发送请求
                    my_wifi_uart_send((const uint8_t *)msg.pData, (uint16_t)msg.DataLen);
                    MY_FREE_BUFFER(msg.pData);
                    msg.pData = NULL;
                }
                break;

            default:
                break;
        }
    }
}

/********************************************************************
**函数名称:  my_wifi_uart_init
**入口参数:  tid      ---        指向线程 ID 变量的指针
**出口参数:  tid      ---        存储启动后的线程 ID
**函数功能:  初始化磁吸串口模块并启动线程
**返回值:    0 表示成功，其他表示失败
*********************************************************************/
int my_wifi_uart_init(k_tid_t *tid)
{
    int err;

    if (tid == NULL)
    {
        return -EINVAL;
    }

    if (!device_is_ready(s_wifi_uart_dev))
    {
        MY_LOG_ERR("WIFI UART device not ready");
        return -ENODEV;
    }

    /* 打开WIFI模组电源（P2.08） */
    err = my_wifi_pwr_on(true);
    if (err != 0)
    {
        MY_LOG_ERR("WIFI power on failed: %d", err);
        return err;
    }

    // 初始化串口接收循环缓冲区
    my_rb_init(&s_wifi_uart_rb, s_wifi_uart_rb_buf, WIFI_UART_RB_SIZE);

    // 初始值为1(表示UART空闲)
    k_sem_init(&s_wifi_uart_tx_done_sem, 1, 1);

    // 设置 UART 异步回调
    err = uart_callback_set(s_wifi_uart_dev, wifi_uart_cb, NULL);
    if (err != 0)
    {
        MY_LOG_ERR("Failed to set WIFI UART callback: %d", err);
        return err;
    }

    // 初始化WIFI串口到统一PM框架，默认保持UART挂起态
    err = my_pm_device_register(MY_PM_DEV_WIFI_UART, &s_wifi_uart_pm_ops);
    if (err < 0)
    {
        MY_LOG_ERR("WIFI UART PM registration failed: %d", err);
        return err;
    }

    // 配置WIFI唤醒引脚(P0.02)为输入，外部上拉，下降沿中断
    if (!gpio_is_ready_dt(&s_wifi_wake_gpio))
    {
        MY_LOG_ERR("WIFI Wake GPIO not ready");
        return -ENODEV;
    }

    err = gpio_pin_configure_dt(&s_wifi_wake_gpio, GPIO_INPUT);
    if (err)
    {
        MY_LOG_ERR("Failed to configure WIFI Wake GPIO (err %d)", err);
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(&s_wifi_wake_gpio, GPIO_INT_EDGE_TO_ACTIVE);
    if (err)
    {
        MY_LOG_ERR("Failed to configure WIFI Wake interrupt (err %d)", err);
        return err;
    }

    gpio_init_callback(&s_wifi_wake_cb_data, wifi_wake_pin_isr, BIT(s_wifi_wake_gpio.pin));
    err = gpio_add_callback(s_wifi_wake_gpio.port, &s_wifi_wake_cb_data);
    if (err)
    {
        MY_LOG_ERR("Failed to add WIFI Wake callback (err %d)", err);
        return err;
    }

    // 初始化阶段先注册默认测试接收回调，便于外部串口工具直接联调
    my_wifi_uart_register_rx_handler(wifi_uart_test_rx_handler);

#if WIFI_UART_DEBUG_ENABLE
    err = my_pm_device_resume(MY_PM_DEV_WIFI_UART);
    if (err < 0)
    {
        MY_LOG_ERR("Failed to keep WIFI UART resumed in debug mode: %d", err);
        return err;
    }
#endif
    // 初始化消息队列
    my_init_msg_handler(MOD_WIFI_UART, &my_wifi_uart_msgq);

    *tid = k_thread_create(&s_my_wifi_uart_task_data, my_wifi_uart_task_stack,
                           K_THREAD_STACK_SIZEOF(my_wifi_uart_task_stack),
                           my_wifi_uart_task, NULL, NULL, NULL,
                           MY_WIFI_TASK_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(*tid, "MY_WIFI_UART");

    MY_LOG_INF("WIFI UART module initialized");
    return 0;
}
