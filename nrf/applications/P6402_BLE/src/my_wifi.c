/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_wifi.c
**文件描述:        wifi api implements
**当前版本:        V1.0
**作    者:        Felix Tang (tangchaofa@jimiiot.com)
**完成日期:        2026.08.04
*********************************************************************/
/* 必须在包含 my_comm.h 之前定义 BLE_LOG_MODULE_ID，避免与 my_ble_log.h 中的默认定义冲突 */
#define BLE_LOG_MODULE_ID BLE_LOG_MOD_WIFI

#include "my_comm.h"

LOG_MODULE_REGISTER(my_wifi, LOG_LEVEL_INF);

#define WIFI_UART_BUF_SIZE               256
#define WIFI_UART_TX_BUFFER_SIZE         2048
#define WIFI_UART_RB_SIZE                512
#define WIFI_UART_TX_WAIT_MS             200
// WIFI串口发送唤醒窗口：预留休眠/唤醒机制，窗口内认为链路仍处于活跃态
#define WIFI_UART_SEND_WAKEUP_WINDOW_MS  2500
// WIFI串口空闲超时时间：预留后续挂起机制，当前调试模式下不会真正执行挂起
#define WIFI_UART_IDLE_TIMEOUT_MS        3000
// 当前WIFI串口无独立唤醒中断脚，故默认保持调试常开模式
#define WIFI_UART_DEBUG_ENABLE           1

// AT 指令响应等待超时预设（毫秒）
#define WIFI_AT_SHORT_TIMEOUT_MS         1000   // AT 测试、简单查询指令
#define WIFI_AT_MED_TIMEOUT_MS           5000   // TCP 连接、数据发送
#define WIFI_AT_LONG_TIMEOUT_MS          15000  // Wi-Fi 配网连接
#define WIFI_AT_CONNECT_TIMEOUT_MS       30000  // TCP 连接（含 DNS 解析）

// AT 响应最大缓冲长度
#define WIFI_AT_RESP_BUF_SIZE            512

// AT 指令 busy 重试参数
#define WIFI_AT_BUSY_RETRY_COUNT         3       // busy 最大重试次数
#define WIFI_AT_BUSY_RETRY_DELAY_MS      200     // busy 重试前等待（毫秒）
#define WIFI_AT_GUARD_DELAY_MS           50      // 指令成功后保护延时（毫秒）

// AT 响应结果码
#define WIFI_AT_RESULT_OK      0    // 成功（收到 OK / > / SEND OK）
#define WIFI_AT_RESULT_ERROR  -1    // 永久失败（收到 ERROR / SEND FAIL）
#define WIFI_AT_RESULT_BUSY   -2    // 模组忙，可重试（收到 busy p...）
#define WIFI_AT_RESULT_PENDING -3   // 初始待定状态

/* AT 指令响应等待状态机 */
typedef enum
{
    WIFI_AT_IDLE,          // 空闲，无等待中的指令
    WIFI_AT_WAIT_OK,       // 等待 OK / ERROR（通用指令）
    WIFI_AT_WAIT_PROMPT,   // 等待 > 提示符（AT+CIPSEND 第一阶段）
    WIFI_AT_WAIT_SEND_OK,  // 等待 SEND OK / SEND FAIL（AT+CIPSEND 第二阶段）
} wifi_at_state_t;

/* AT 指令响应上下文 */
typedef struct
{
    struct k_sem resp_sem;                          // 同步信号量
    char resp_buf[WIFI_AT_RESP_BUF_SIZE];           // 响应缓冲区（累积多行响应）
    uint16_t resp_len;                              // 响应缓冲区已使用长度
    wifi_at_state_t state;                          // 当前等待状态
    int result;                                     // WIFI_AT_RESULT_OK / ERROR / BUSY / PENDING

    my_wifi_tcp_recv_cb_t tcp_recv_cb;              // +IPD 数据接收回调
    my_wifi_event_cb_t   wifi_event_cb;             // Wi-Fi 事件回调
} wifi_at_ctx_t;

#define WIFI_UART_NODE DT_ALIAS(wifi_uart)
static const struct device *s_wifi_uart_dev = DEVICE_DT_GET(WIFI_UART_NODE);

typedef struct
{
    struct k_timer idle_timer;               // 空闲定时器：预留给后续WIFI串口挂起场景
    int64_t send_wakeup_expire_timestamp_ms; // 发送唤醒窗口到期时间戳（毫秒）
    bool active;                             // 当前 UART RX 是否处于使能状态
    bool tx_busy;                            // 当前是否存在正在进行的异步发送
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
static uint8_t s_wifi_uart_rx_buf_1[WIFI_UART_BUF_SIZE];
static uint8_t s_wifi_uart_rx_buf_2[WIFI_UART_BUF_SIZE];
static uint8_t *s_wifi_uart_next_buf = s_wifi_uart_rx_buf_2;
static uint8_t s_wifi_uart_rb_buf[WIFI_UART_RB_SIZE];
static ring_buffer_t s_wifi_uart_rb;

static wifi_at_ctx_t s_wifi_at_ctx;
char g_wifiSsid[MAX_WIFI_SSID_LEN] = {0}; // 用于存储 Wi-Fi SSID
char g_wifiPasswd[MAX_WIFI_PASSWD_LEN] = {0}; // 用于存储 Wi-Fi 密码

K_MSGQ_DEFINE(my_wifi_uart_msgq, sizeof(msg_t), 10, 4);
K_THREAD_STACK_DEFINE(my_wifi_uart_task_stack, MY_WIFI_TASK_STACK_SIZE);
static struct k_thread s_my_wifi_uart_task_data;

// wifi connect api
void my_wifi_connect(const char *ssid, const char *passwd)
{
    if (ssid != NULL)
    {
        strncpy(g_wifiSsid, ssid, MAX_WIFI_SSID_LEN - 1);
        g_wifiSsid[MAX_WIFI_SSID_LEN - 1] = '\0';
    }

    if (passwd != NULL)
    {
        strncpy(g_wifiPasswd, passwd, MAX_WIFI_PASSWD_LEN - 1);
        g_wifiPasswd[MAX_WIFI_PASSWD_LEN - 1] = '\0';
    }

    // 发消息到GPRS模块，通知其执行Wi-Fi连接操作
    my_send_msg(MOD_WIFI, MOD_GPRS, MY_MSG_WIFI_CONNECT);
}

/********************************************************************
**函数名称:  wifi_uart_idle_timer_handler
**入口参数:  timer    ---        定时器句柄
**出口参数:  无
**函数功能:  WIFI串口空闲定时器回调，通知线程执行挂起判断
**返回值:    无
*********************************************************************/
static void wifi_uart_idle_timer_handler(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    my_send_msg(MOD_WIFI, MOD_WIFI, MY_MSG_UART_IDLE);
}

/********************************************************************
**函数名称:  wifi_uart_pm_init
**入口参数:  无
**出口参数:  无
**函数功能:  WIFI串口电源管理初始化，重置上下文状态
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
**函数功能:  WIFI串口挂起处理，关闭 RX 接收以节省功耗
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
**函数功能:  WIFI串口恢复处理，重新启用 RX 接收
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
**函数功能:  确保WIFI串口处于活跃态，若已挂起则执行恢复
**返回值:    0 --- 已活跃或恢复成功
**           其他 --- 错误码
*********************************************************************/
static int wifi_uart_ensure_active(void)
{
    int ret;

    if (!s_wifi_uart_ctx.active)
    {
        // 发送前主动恢复 PM，避免 UART 处于挂起态时直接下发发送请求
        ret = my_pm_device_resume(MY_PM_DEV_WIFI);
        if (ret < 0)
        {
            MY_LOG_ERR("Failed to resume WiFi UART: %d", ret);
            return ret;
        }
    }

    return 0;
}

/********************************************************************
**函数名称:  wifi_uart_can_suspend
**入口参数:  无
**出口参数:  无
**函数功能:  检查WIFI串口是否满足挂起条件
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
**函数功能:  刷新WIFI串口活跃定时器，延迟挂起以维持通信
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
**函数功能:  刷新WIFI串口发送唤醒窗口
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
**函数功能:  判断当前是否已超出WIFI串口发送唤醒窗口
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
**函数名称:  wifi_at_resp_append
**入口参数:  line     ---        待追加的响应行
**出口参数:  无
**函数功能:  将单行响应追加到 AT 响应缓冲区
**返回值:    无
*********************************************************************/
static void wifi_at_resp_append(const char *line)
{
    uint16_t line_len = strlen(line);
    uint16_t remaining = sizeof(s_wifi_at_ctx.resp_buf) - s_wifi_at_ctx.resp_len;

    if (remaining < line_len + 2)
    {
        MY_LOG_WRN("AT resp buf full, drop line: %s", line);
        return;
    }

    if (s_wifi_at_ctx.resp_len > 0)
    {
        s_wifi_at_ctx.resp_buf[s_wifi_at_ctx.resp_len++] = '\n';
    }
    memcpy(&s_wifi_at_ctx.resp_buf[s_wifi_at_ctx.resp_len], line, line_len);
    s_wifi_at_ctx.resp_len += line_len;
    s_wifi_at_ctx.resp_buf[s_wifi_at_ctx.resp_len] = '\0';
}

/********************************************************************
**函数名称:  wifi_at_parse_ipd
**入口参数:  line     ---        +IPD 原始行
**出口参数:  无
**函数功能:  解析 +IPD,<len>:<data> 或 +IPD,<len>,<ip>,<port>:<data>
**           提取数据负载并调用注册的 TCP 接收回调
**返回值:    0 表示解析成功，负值表示解析失败
*********************************************************************/
static int wifi_at_parse_ipd(char *line)
{
    char *comma;
    char *colon;
    int data_len;

    /* 跳过 "+IPD," 前缀 */
    comma = strchr(line, ',');
    if (!comma)
    {
        return -EINVAL;
    }

    data_len = atoi(comma + 1);
    if (data_len <= 0)
    {
        return -EINVAL;
    }

    /* 查找最后一个冒号，其后为实际数据负载 */
    colon = strrchr(line, ':');
    if (!colon)
    {
        return -EINVAL;
    }

    /* colon+1 指向数据负载起始，长度由 data_len 指定 */
    if (s_wifi_at_ctx.tcp_recv_cb)
    {
        s_wifi_at_ctx.tcp_recv_cb((uint8_t *)(colon + 1), (uint16_t)data_len);
    }

    return 0;
}

/********************************************************************
**函数名称:  wifi_at_handle_line
**入口参数:  line     ---        已去行尾符的单行 AT 响应
**出口参数:  无
**函数功能:  根据当前 AT 等待状态和行内容，进行分类处理：
**           - 同步响应：OK / ERROR / > / SEND OK / SEND FAIL
**           - 异步事件：WIFI CONNECTED / WIFI GOT IP / WIFI DISCONNECT / +IPD
**           - 其他数据行：累积到响应缓冲区供上层解析
**返回值:    无
*********************************************************************/
static void wifi_at_handle_line(char *line)
{
    int line_len;

    /* 跳过空行 */
    line_len = strlen(line);
    if (line_len == 0)
    {
        return;
    }

    /* ---- 处理 +IPD 异步数据上报（任意状态下均处理） ---- */
    if (strncmp(line, "+IPD,", 5) == 0)
    {
        MY_LOG_INF("AT recv IPD: %s", line);
        wifi_at_parse_ipd(line);
        return;
    }

    /* ---- 处理模组忙响应（任意状态下均处理，设置 BUSY 结果唤醒调用者重试） ---- */
    if (strncmp(line, "busy p", 5) == 0) //busy p...
    {
        MY_LOG_WRN("AT module busy: %s", line);
        s_wifi_at_ctx.result = WIFI_AT_RESULT_BUSY;
        k_sem_give(&s_wifi_at_ctx.resp_sem);
        return;
    }

    /* ---- 处理 ERR CODE 行（可能后跟 busy，累积到缓冲区供日志排查） ---- */
    if (strncmp(line, "ERR CODE:", 9) == 0) //ERR CODE:0x010b0000
    {
        MY_LOG_WRN("AT error code: %s", line);
        wifi_at_resp_append(line);
        return;
    }

    /* ---- 处理 Wi-Fi 异步事件上报 ---- */
    if (strcmp(line, "WIFI CONNECTED") == 0)
    {
        MY_LOG_INF("AT event: WIFI CONNECTED");
        if (s_wifi_at_ctx.wifi_event_cb)
        {
            s_wifi_at_ctx.wifi_event_cb(WIFI_EVENT_CONNECTED);
        }
        wifi_at_resp_append(line);
        return;
    }

    if (strcmp(line, "WIFI GOT IP") == 0)
    {
        MY_LOG_INF("AT event: WIFI GOT IP");
        if (s_wifi_at_ctx.wifi_event_cb)
        {
            s_wifi_at_ctx.wifi_event_cb(WIFI_EVENT_GOT_IP);
        }
        wifi_at_resp_append(line);
        return;
    }

    if (strcmp(line, "WIFI DISCONNECT") == 0)
    {
        MY_LOG_INF("AT event: WIFI DISCONNECT");
        if (s_wifi_at_ctx.wifi_event_cb)
        {
            s_wifi_at_ctx.wifi_event_cb(WIFI_EVENT_DISCONNECTED);
        }
        wifi_at_resp_append(line);
        return;
    }

    /* ---- 处理同步状态机响应 ---- */
    switch (s_wifi_at_ctx.state)
    {
        case WIFI_AT_WAIT_OK:
            if (strcmp(line, "OK") == 0)
            {
                s_wifi_at_ctx.result = WIFI_AT_RESULT_OK;
                k_sem_give(&s_wifi_at_ctx.resp_sem);
            }
            else if (strncmp(line, "ERROR", 5) == 0)
            {
                s_wifi_at_ctx.result = WIFI_AT_RESULT_ERROR;
                k_sem_give(&s_wifi_at_ctx.resp_sem);
            }
            else if (strcmp(line, "SEND OK") == 0)
            {
                /* 部分场景下 SEND OK 作为最终应答 */
                s_wifi_at_ctx.result = WIFI_AT_RESULT_OK;
                k_sem_give(&s_wifi_at_ctx.resp_sem);
            }
            else
            {
                /* 数据行：累积到响应缓冲区 */
                wifi_at_resp_append(line);
            }
            break;

        case WIFI_AT_WAIT_PROMPT:
            if (line[0] == '>')
            {
                /* 收到 > 提示符，可以发送数据 */
                s_wifi_at_ctx.result = WIFI_AT_RESULT_OK;
                k_sem_give(&s_wifi_at_ctx.resp_sem);
            }
            else if (strncmp(line, "ERROR", 5) == 0)
            {
                s_wifi_at_ctx.result = WIFI_AT_RESULT_ERROR;
                k_sem_give(&s_wifi_at_ctx.resp_sem);
            }
            else if (strcmp(line, "OK") != 0)
            {
                /* OK 在 > 之前出现时忽略（ESP32 有时先回 OK 再回 >） */
                wifi_at_resp_append(line);
            }
            break;

        case WIFI_AT_WAIT_SEND_OK:
            if (strcmp(line, "SEND OK") == 0)
            {
                s_wifi_at_ctx.result = WIFI_AT_RESULT_OK;
                k_sem_give(&s_wifi_at_ctx.resp_sem);
            }
            else if (strcmp(line, "SEND FAIL") == 0 ||
                     strncmp(line, "ERROR", 5) == 0)
            {
                s_wifi_at_ctx.result = WIFI_AT_RESULT_ERROR;
                k_sem_give(&s_wifi_at_ctx.resp_sem);
            }
            else
            {
                wifi_at_resp_append(line);
            }
            break;

        case WIFI_AT_IDLE:
        default:
            /* 空闲状态下收到非预期数据，仅记录日志 */
            MY_LOG_WRN("AT unexpected line in IDLE: %s", line);
            break;
    }
}

/********************************************************************
**函数名称:  my_wifi_parse_cmd
**入口参数:  cmd      ---        AT 响应单行数据（已去行尾符）
**           cmd_len  ---        数据长度
**出口参数:  无
**函数功能:  WIFI串口消息解析入口，委托 wifi_at_handle_line 处理
**返回值:    0 始终返回 0
*********************************************************************/
int my_wifi_parse_cmd(char *cmd, int cmd_len)
{
    MY_LOG_INF("%s: len[%d], %s", __func__, cmd_len, cmd);

    wifi_at_handle_line(cmd);

    return 0;
}

void my_wifi_handle_recv(uint8_t *pData, uint32_t iLen)
{
    static char command[MAX_CMD_LEN] = {0};
    static uint32_t index = 0;
    uint32_t i;

    for (i = 0; i < iLen; i++)
    {
        if (pData[i] == '\r' || pData[i] == '\n') // 回车是\r 为了兼容同时处理 \n
        {
            my_wifi_parse_cmd(command, index);

            command[0] = 0;
            index = 0;

            // 如果下个字符是\n，跳过
            if (pData[i + 1] == '\n')
            {
                i++;
            }
        }
        else if (index < (MAX_CMD_LEN - 1))
        {
            command[index++] = pData[i];
            command[index] = '\0';
        }
    }
}

/********************************************************************
**函数名称:  wifi_uart_cb
**入口参数:  dev       ---        UART 设备句柄
**           evt       ---        UART 事件结构体
**           user_data ---        用户自定义数据
**出口参数:  无
**函数功能:  WIFI串口异步事件回调
**返回值:    无
**注意事项:  中断上下文只做缓冲搬运和投递消息，不在此处做复杂业务处理
*********************************************************************/
static void wifi_uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    switch (evt->type)
    {
        case UART_TX_DONE:
            my_send_msg(MOD_WIFI, MOD_WIFI, MY_MSG_UART_TX_DONE);
            // 传输完成，释放信号量
            k_sem_give(&s_wifi_uart_tx_done_sem);
            break;

        case UART_RX_RDY:
            // 将中断回调接收到的数据搬运到环形缓冲区，在线程中统一处理
            my_rb_write(&s_wifi_uart_rb, &evt->data.rx.buf[evt->data.rx.offset], evt->data.rx.len);
            // 通知wifi_uart线程读取循环缓冲区数据
            my_send_msg(MOD_WIFI, MOD_WIFI, MY_MSG_UART_REV);
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
            my_send_msg(MOD_WIFI, MOD_WIFI, MY_MSG_UART_TX_ABORTED);
            k_sem_give(&s_wifi_uart_tx_done_sem);
            break;

        default:
            break;
    }
}

// 主动向WiFi模块发送串口消息, 返回0表示成功，其他表示失败
int my_wifi_send_msg_data(char *msg)
{
    msg_t sendMsg;
    char *p_msg = NULL;
    int len = strlen(msg);
    int ret = -1;

    if (0 == len)
    {
        MY_LOG_ERR("empty msg!");
        return ret;
    }

    MY_MALLOC_BUFFER(p_msg, len + 3);
    if (p_msg == NULL)
    {
        MY_LOG_ERR("p_msg malloc failed!");
        return ret;
    }

    memset(p_msg, 0, len + 3);
    // NOTE: 不包含字符串结束符
    memcpy(p_msg, msg, len);
    // 添加\r\n结束符
    strcat(p_msg, "\r\n");
    sendMsg.msgID = MY_MSG_UART_SEND;
    sendMsg.pData = p_msg;
    sendMsg.DataLen = len + 2;

    my_send_msg_data(MOD_MAIN, MOD_WIFI, &sendMsg);
    return 0;
}

/********************************************************************
**函数名称:  my_wifi_at_send_and_wait
**入口参数:  cmd         ---        AT 指令字符串
**           timeout_ms  ---        等待超时（毫秒）
**出口参数:  无
**函数功能:  发送 AT 指令并通过信号量同步等待 OK/ERROR 响应
**           指令通过消息队列异步发送，本函数阻塞等待响应完成
**返回值:    0 表示收到 OK，负值表示错误码（-EIO / -ETIMEDOUT）
**注意事项:  1. 只能从非 wifi_uart_task 线程调用，否则会死锁
**           2. 调用前会自动复位响应上下文和信号量
*********************************************************************/
int my_wifi_at_send_and_wait(const char *cmd, uint32_t timeout_ms)
{
    int ret;
    int attempt;

    if (!cmd || strlen(cmd) == 0)
    {
        return -EINVAL;
    }

    for (attempt = 0; attempt <= WIFI_AT_BUSY_RETRY_COUNT; attempt++)
    {
        /* 复位响应上下文 */
        s_wifi_at_ctx.state = WIFI_AT_WAIT_OK;
        s_wifi_at_ctx.resp_len = 0;
        memset(s_wifi_at_ctx.resp_buf, 0, sizeof(s_wifi_at_ctx.resp_buf));
        s_wifi_at_ctx.result = WIFI_AT_RESULT_PENDING;
        k_sem_reset(&s_wifi_at_ctx.resp_sem);

        /* 通过消息队列异步发送 AT 指令 */
        ret = my_wifi_send_msg_data((char *)cmd);
        if (ret != 0)
        {
            s_wifi_at_ctx.state = WIFI_AT_IDLE;
            MY_LOG_ERR("AT send failed: %d, cmd: %s", ret, cmd);
            return ret;
        }

        /* 阻塞等待响应 */
        ret = k_sem_take(&s_wifi_at_ctx.resp_sem, K_MSEC(timeout_ms));
        if (ret != 0)
        {
            s_wifi_at_ctx.state = WIFI_AT_IDLE;
            MY_LOG_ERR("AT cmd timeout(%ums): %s", timeout_ms, cmd);
            return -ETIMEDOUT;
        }

        /* 检查是否收到 busy 响应 */
        if (s_wifi_at_ctx.result == WIFI_AT_RESULT_BUSY)
        {
            if (attempt < WIFI_AT_BUSY_RETRY_COUNT)
            {
                MY_LOG_WRN("AT cmd busy, retry %d/%d after %dms: %s",
                           attempt + 1, WIFI_AT_BUSY_RETRY_COUNT,
                           WIFI_AT_BUSY_RETRY_DELAY_MS, cmd);
                k_sleep(K_MSEC(WIFI_AT_BUSY_RETRY_DELAY_MS));
                continue;
            }
            else
            {
                s_wifi_at_ctx.state = WIFI_AT_IDLE;
                MY_LOG_ERR("AT cmd busy exhausted retries: %s", cmd);
                return -EBUSY;
            }
        }

        /* 非 busy 结果，退出重试循环 */
        break;
    }

    s_wifi_at_ctx.state = WIFI_AT_IDLE;

    if (s_wifi_at_ctx.result != WIFI_AT_RESULT_OK)
    {
        MY_LOG_ERR("AT cmd error, cmd: %s, resp: %s", cmd, s_wifi_at_ctx.resp_buf);
        return -EIO;
    }

    /* 指令成功后插入保护延时，避免下一条指令发送时模组尚未完成内部清理 */
    k_sleep(K_MSEC(WIFI_AT_GUARD_DELAY_MS));

    return 0;
}

/********************************************************************
**函数名称:  my_wifi_at_get_resp
**入口参数:  无
**出口参数:  无
**函数功能:  获取最后一次 AT 同步指令的响应缓冲区内容
**返回值:    响应字符串指针（只读）
*********************************************************************/
const char *my_wifi_at_get_resp(void)
{
    return s_wifi_at_ctx.resp_buf;
}

/* ========== Wi-Fi 配网接口实现 ========== */

/********************************************************************
**函数名称:  my_wifi_set_mode
**入口参数:  mode     ---        Wi-Fi 工作模式（0/1/2/3）
**出口参数:  无
**函数功能:  设置 ESP32-C2 Wi-Fi 工作模式（AT+CWMODE）
**返回值:    0 表示成功，负值表示错误码
*********************************************************************/
int my_wifi_set_mode(uint8_t mode)
{
    char cmd[32];

    if (mode > 3)
    {
        return -EINVAL;
    }

    snprintf(cmd, sizeof(cmd), "AT+CWMODE=%u", mode);
    return my_wifi_at_send_and_wait(cmd, WIFI_AT_SHORT_TIMEOUT_MS);
}

/********************************************************************
**函数名称:  my_wifi_connect_ap
**入口参数:  ssid     ---        Wi-Fi 热点名称
**           password ---        Wi-Fi 密码
**出口参数:  无
**函数功能:  连接指定 Wi-Fi 路由器（AT+CWJAP）
**           连接过程包含扫描、关联、DHCP，最长等待 15 秒
**返回值:    0 表示成功，负值表示错误码
*********************************************************************/
int my_wifi_connect_ap(const char *ssid, const char *password)
{
    char cmd[128];

    if (!ssid || !password || strlen(ssid) == 0)
    {
        return -EINVAL;
    }

    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
    return my_wifi_at_send_and_wait(cmd, WIFI_AT_LONG_TIMEOUT_MS);
}

/********************************************************************
**函数名称:  my_wifi_disconnect_ap
**入口参数:  无
**出口参数:  无
**函数功能:  断开当前 Wi-Fi 路由器连接（AT+CWQAP）
**返回值:    0 表示成功，负值表示错误码
*********************************************************************/
int my_wifi_disconnect_ap(void)
{
    return my_wifi_at_send_and_wait("AT+CWQAP", WIFI_AT_SHORT_TIMEOUT_MS);
}

/********************************************************************
**函数名称:  my_wifi_get_conn_state
**入口参数:  无
**出口参数:  无
**函数功能:  查询 Wi-Fi 连接状态（AT+CWSTATE?）
**           从响应中解析 +CWSTATE:<state> 并返回状态值
**返回值:    >=0 表示连接状态（my_wifi_conn_state_t），负值表示错误码
*********************************************************************/
int my_wifi_get_conn_state(void)
{
    int ret;
    const char *resp;
    char *ptr;
    int state;

    ret = my_wifi_at_send_and_wait("AT+CWSTATE?", WIFI_AT_SHORT_TIMEOUT_MS);
    if (ret != 0)
    {
        return ret;
    }

    resp = my_wifi_at_get_resp();
    /* 响应格式：+CWSTATE:<state> */
    ptr = strstr(resp, "+CWSTATE:");
    if (!ptr)
    {
        return -EIO;
    }

    state = atoi(ptr + strlen("+CWSTATE:"));
    return state;
}

/********************************************************************
**函数名称:  my_wifi_get_ip
**入口参数:  ip_buf   ---        IP 地址输出缓冲区
**           mac_buf  ---        MAC 地址输出缓冲区
**           buf_len  ---        缓冲区长度
**出口参数:  ip_buf   ---        填充 STA IP 地址字符串
**           mac_buf  ---        填充 STA MAC 地址字符串
**函数功能:  查询模组 STA 本地 IP 和 MAC 地址（AT+CIFSR）
**返回值:    0 表示成功，负值表示错误码
*********************************************************************/
int my_wifi_get_ip(char *ip_buf, char *mac_buf, uint8_t buf_len)
{
    int ret;
    const char *resp;
    char *ptr;
    char *end;

    if (0 == buf_len || !ip_buf || !mac_buf)
    {
        return -EINVAL;
    }

    ret = my_wifi_at_send_and_wait("AT+CIFSR", WIFI_AT_SHORT_TIMEOUT_MS);
    if (ret != 0)
    {
        return ret;
    }

    resp = my_wifi_at_get_resp();

    /* 解析 +CIFSR:STAIP,"<ip>" */
    if (ip_buf)
    {
        memset(ip_buf, 0, buf_len);
        ptr = strstr(resp, "+CIFSR:STAIP,\"");
        if (ptr)
        {
            ptr += strlen("+CIFSR:STAIP,\"");
            end = strchr(ptr, '\"');
            if (end)
            {
                uint16_t copy_len = (end - ptr) < buf_len ? (end - ptr) : (buf_len - 1);
                memcpy(ip_buf, ptr, copy_len);
            }
        }
    }

    /* 解析 +CIFSR:STAMAC,"<mac>" */
    if (mac_buf)
    {
        memset(mac_buf, 0, buf_len);
        ptr = strstr(resp, "+CIFSR:STAMAC,\"");
        if (ptr)
        {
            ptr += strlen("+CIFSR:STAMAC,\"");
            end = strchr(ptr, '\"');
            if (end)
            {
                uint16_t copy_len = (end - ptr) < buf_len ? (end - ptr) : (buf_len - 1);
                memcpy(mac_buf, ptr, copy_len);
            }
        }
    }

    return 0;
}

/********************************************************************
**函数名称:  my_wifi_set_auto_conn
**入口参数:  enable   ---        1=开启自动重连，0=关闭
**出口参数:  无
**函数功能:  设置上电自动重连上一次保存的 Wi-Fi（AT+CWAUTOCONN）
**返回值:    0 表示成功，负值表示错误码
*********************************************************************/
int my_wifi_set_auto_conn(uint8_t enable)
{
    char cmd[32];

    snprintf(cmd, sizeof(cmd), "AT+CWAUTOCONN=%u", enable ? 1 : 0);
    return my_wifi_at_send_and_wait(cmd, WIFI_AT_SHORT_TIMEOUT_MS);
}

/* ========== Wi-Fi 固件基础信息接口实现 ========== */

/********************************************************************
**函数名称:  my_wifi_get_fw_info
**入口参数:  info_buf ---        固件信息输出缓冲区
**           buf_len  ---        缓冲区长度
**出口参数:  info_buf ---        填充固件版本信息字符串
**函数功能:  查询 AT 固件版本、SDK 版本等信息（AT+GMR）
**           将多行响应内容拷贝到输出缓冲区
**返回值:    0 表示成功，负值表示错误码
*********************************************************************/
int my_wifi_get_fw_info(char *info_buf, uint16_t buf_len)
{
    int ret;
    const char *resp;

    if (!info_buf || buf_len == 0)
    {
        return -EINVAL;
    }

    ret = my_wifi_at_send_and_wait("AT+GMR", WIFI_AT_SHORT_TIMEOUT_MS);
    if (ret != 0)
    {
        return ret;
    }

    resp = my_wifi_at_get_resp();
    strncpy(info_buf, resp, buf_len - 1);
    info_buf[buf_len - 1] = '\0';

    return 0;
}

/* ========== TCP/IP 连接与数据收发接口实现 ========== */

/********************************************************************
**函数名称:  my_wifi_tcp_set_mux
**入口参数:  mode     ---        0=单连接，1=多连接（最多 5 路）
**出口参数:  无
**函数功能:  设置 TCP 多连接模式（AT+CIPMUX）
**返回值:    0 表示成功，负值表示错误码
*********************************************************************/
int my_wifi_tcp_set_mux(uint8_t mode)
{
    char cmd[32];

    if (mode > 1)
    {
        return -EINVAL;
    }

    snprintf(cmd, sizeof(cmd), "AT+CIPMUX=%u", mode);
    return my_wifi_at_send_and_wait(cmd, WIFI_AT_SHORT_TIMEOUT_MS);
}

/********************************************************************
**函数名称:  my_wifi_tcp_connect
**入口参数:  host     ---        服务器 IP 地址或域名
**           port     ---        服务器端口号
**出口参数:  无
**函数功能:  建立 TCP 客户端连接（AT+CIPSTART）
**           单连接模式下连接指定服务器
**返回值:    0 表示成功，负值表示错误码
*********************************************************************/
int my_wifi_tcp_connect(const char *host, uint16_t port)
{
    char cmd[128];

    if (!host || strlen(host) == 0 || port == 0)
    {
        return -EINVAL;
    }

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, port);
    return my_wifi_at_send_and_wait(cmd, WIFI_AT_CONNECT_TIMEOUT_MS);
}

/********************************************************************
**函数名称:  my_wifi_tcp_close
**入口参数:  无
**出口参数:  无
**函数功能:  断开当前 TCP 连接（AT+CIPCLOSE）
**返回值:    0 表示成功，负值表示错误码
*********************************************************************/
int my_wifi_tcp_close(void)
{
    return my_wifi_at_send_and_wait("AT+CIPCLOSE", WIFI_AT_SHORT_TIMEOUT_MS);
}

/********************************************************************
**函数名称:  my_wifi_tcp_send
**入口参数:  data     ---        待发送数据指针
**           len      ---        数据长度（<=1536 字节）
**出口参数:  无
**函数功能:  通过 TCP 发送数据到服务器
**           两阶段流程：
**           1. 发送 AT+CIPSEND=<len>，等待 > 提示符
**           2. 通过 UART 直发原始数据（无 \r\n 追加），等待 SEND OK
**返回值:    0 表示成功，负值表示错误码
**注意事项:  数据通过 my_wifi_uart_send 直接发送，不会追加 \r\n
*********************************************************************/
int my_wifi_tcp_send(const uint8_t *data, uint16_t len)
{
    char cmd[32];
    int ret;
    int attempt;

    if (!data || len == 0)
    {
        return -EINVAL;
    }

    if (len > 8192)
    {
        MY_LOG_ERR("TCP send data too large: %u", len);
        return -EINVAL;
    }

    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", len);

    for (attempt = 0; attempt <= WIFI_AT_BUSY_RETRY_COUNT; attempt++)
    {
        /* ---- 第一阶段：发送 AT+CIPSEND=<len>，等待 > 提示符 ---- */
        s_wifi_at_ctx.state = WIFI_AT_WAIT_PROMPT;
        s_wifi_at_ctx.result = WIFI_AT_RESULT_PENDING;
        k_sem_reset(&s_wifi_at_ctx.resp_sem);

        ret = my_wifi_send_msg_data(cmd);
        if (ret != 0)
        {
            s_wifi_at_ctx.state = WIFI_AT_IDLE;
            MY_LOG_ERR("AT+CIPSEND send failed: %d", ret);
            return ret;
        }

        ret = k_sem_take(&s_wifi_at_ctx.resp_sem, K_MSEC(WIFI_AT_MED_TIMEOUT_MS));
        if (ret != 0)
        {
            s_wifi_at_ctx.state = WIFI_AT_IDLE;
            MY_LOG_ERR("AT+CIPSEND wait prompt timeout");
            return -ETIMEDOUT;
        }

        /* 收到 busy：重试整个 AT+CIPSEND 流程 */
        if (s_wifi_at_ctx.result == WIFI_AT_RESULT_BUSY)
        {
            if (attempt < WIFI_AT_BUSY_RETRY_COUNT)
            {
                MY_LOG_WRN("AT+CIPSEND busy, retry %d/%d after %dms",
                           attempt + 1, WIFI_AT_BUSY_RETRY_COUNT,
                           WIFI_AT_BUSY_RETRY_DELAY_MS);
                k_sleep(K_MSEC(WIFI_AT_BUSY_RETRY_DELAY_MS));
                continue;
            }
            else
            {
                s_wifi_at_ctx.state = WIFI_AT_IDLE;
                MY_LOG_ERR("AT+CIPSEND busy exhausted retries");
                return -EBUSY;
            }
        }

        if (s_wifi_at_ctx.result != WIFI_AT_RESULT_OK)
        {
            s_wifi_at_ctx.state = WIFI_AT_IDLE;
            MY_LOG_ERR("AT+CIPSEND error, resp: %s", s_wifi_at_ctx.resp_buf);
            return -EIO;
        }

        /* ---- 第二阶段：发送原始数据（不追加 \r\n） ---- */
        s_wifi_at_ctx.state = WIFI_AT_WAIT_SEND_OK;
        s_wifi_at_ctx.result = WIFI_AT_RESULT_PENDING;

        /* 通过 my_wifi_uart_send 直接发送原始数据 */
        ret = my_wifi_uart_send(data, len);
        if (ret != 0)
        {
            s_wifi_at_ctx.state = WIFI_AT_IDLE;
            MY_LOG_ERR("TCP raw data send failed: %d", ret);
            return ret;
        }

        /* 等待 SEND OK */
        ret = k_sem_take(&s_wifi_at_ctx.resp_sem, K_MSEC(WIFI_AT_MED_TIMEOUT_MS));
        if (ret != 0)
        {
            s_wifi_at_ctx.state = WIFI_AT_IDLE;
            MY_LOG_ERR("TCP send wait SEND OK timeout");
            return -ETIMEDOUT;
        }

        /* 收到 busy：本次数据未被接受，需从 AT+CIPSEND 重新开始 */
        if (s_wifi_at_ctx.result == WIFI_AT_RESULT_BUSY)
        {
            if (attempt < WIFI_AT_BUSY_RETRY_COUNT)
            {
                MY_LOG_WRN("TCP data send busy, retry from AT+CIPSEND %d/%d after %dms",
                           attempt + 1, WIFI_AT_BUSY_RETRY_COUNT,
                           WIFI_AT_BUSY_RETRY_DELAY_MS);
                k_sleep(K_MSEC(WIFI_AT_BUSY_RETRY_DELAY_MS));
                continue;
            }
            else
            {
                s_wifi_at_ctx.state = WIFI_AT_IDLE;
                MY_LOG_ERR("TCP data send busy exhausted retries");
                return -EBUSY;
            }
        }

        /* 非 busy 结果，退出重试循环 */
        break;
    }

    s_wifi_at_ctx.state = WIFI_AT_IDLE;

    if (s_wifi_at_ctx.result != WIFI_AT_RESULT_OK)
    {
        MY_LOG_ERR("TCP send failed (SEND FAIL)");
        return -EIO;
    }

    /* 发送成功后插入保护延时 */
    k_sleep(K_MSEC(WIFI_AT_GUARD_DELAY_MS));

    return 0;
}

/********************************************************************
**函数名称:  my_wifi_tcp_set_dip_info
**入口参数:  enable   ---        1=开启远端 IP/端口标识，0=关闭
**出口参数:  无
**函数功能:  设置 +IPD 数据头是否携带远端 IP 和端口（AT+CIPDINFO）
**           开启后 +IPD 格式：+IPD,<len>,<ip>,<port>:<data>
**返回值:    0 表示成功，负值表示错误码
*********************************************************************/
int my_wifi_tcp_set_dip_info(uint8_t enable)
{
    char cmd[32];

    snprintf(cmd, sizeof(cmd), "AT+CIPDINFO=%u", enable ? 1 : 0);
    return my_wifi_at_send_and_wait(cmd, WIFI_AT_SHORT_TIMEOUT_MS);
}

/* ========== 回调注册接口实现 ========== */

/********************************************************************
**函数名称:  my_wifi_register_tcp_recv_cb
**入口参数:  cb       ---        TCP 数据接收回调函数指针
**出口参数:  无
**函数功能:  注册 TCP 数据接收回调，收到 +IPD 时回调
**返回值:    无
*********************************************************************/
void my_wifi_register_tcp_recv_cb(my_wifi_tcp_recv_cb_t cb)
{
    s_wifi_at_ctx.tcp_recv_cb = cb;
}

/********************************************************************
**函数名称:  my_wifi_register_wifi_event_cb
**入口参数:  cb       ---        Wi-Fi 事件回调函数指针
**出口参数:  无
**函数功能:  注册 Wi-Fi 事件回调，发生异步事件时回调
**返回值:    无
*********************************************************************/
void my_wifi_register_wifi_event_cb(my_wifi_event_cb_t cb)
{
    s_wifi_at_ctx.wifi_event_cb = cb;
}

/* ========== 上电初始化流程实现 ========== */

/********************************************************************
**函数名称:  my_wifi_at_init_sequence
**入口参数:  无
**出口参数:  无
**函数功能:  执行 ESP32-C2 标准上电初始化 AT 指令序列：
**           AT（模组在线检测）
**           ATE0（关闭回显）
**           AT+CWMODE=1（Station 模式）
**           AT+CWAUTOCONN=1（上电自动连 Wi-Fi）
**           AT+CIPMUX=0（单 TCP 链路）
**返回值:    0 表示成功，负值表示错误码
*********************************************************************/
int my_wifi_at_init_sequence(void)
{
    int ret;

    /* 1. 模组在线检测 */
    MY_LOG_INF("AT init: test module online");
    ret = my_wifi_at_send_and_wait("AT", WIFI_AT_SHORT_TIMEOUT_MS);
    if (ret != 0)
    {
        MY_LOG_ERR("AT init: module not responding");
        return ret;
    }

    /* 2. 关闭命令回显 */
    MY_LOG_INF("AT init: disable echo");
    ret = my_wifi_at_send_and_wait("ATE0", WIFI_AT_SHORT_TIMEOUT_MS);
    if (ret != 0)
    {
        MY_LOG_WRN("AT init: disable echo failed, continue");
    }

    /* 3. 切换 Wi-Fi Station 模式 */
    MY_LOG_INF("AT init: set STA mode");
    ret = my_wifi_set_mode(WIFI_MODE_STA);
    if (ret != 0)
    {
        MY_LOG_ERR("AT init: set mode failed");
        return ret;
    }

    /* 4. 开启上电自动连接 */
    MY_LOG_INF("AT init: enable auto connect");
    ret = my_wifi_set_auto_conn(1);
    if (ret != 0)
    {
        MY_LOG_ERR("AT init: set auto conn failed");
        return ret;
    }

    /* 5. 单 TCP 链路模式 */
    MY_LOG_INF("AT init: set single connection mode");
    ret = my_wifi_tcp_set_mux(0);
    if (ret != 0)
    {
        MY_LOG_ERR("AT init: set mux failed");
        return ret;
    }

    MY_LOG_INF("AT init sequence completed");
    return 0;
}

/********************************************************************
**函数名称:  my_wifi_uart_send
**入口参数:  data     ---        待发送数据指针
**           len      ---        数据长度
**出口参数:  无
**函数功能:  通过WIFI串口发送指定长度数据
**返回值:    0 表示成功，其他表示失败
*********************************************************************/
int my_wifi_uart_send(const uint8_t *data, uint16_t len)
{
    int ret = 0;
    bool need_send_wakeup = false;
    static uint8_t s_wifi_uart_tx_buf[WIFI_UART_TX_BUFFER_SIZE] = {0};

    if (len > WIFI_UART_TX_BUFFER_SIZE)
    {
        MY_LOG_ERR("WIFI UART TX data too large: %d", len);
        return -EINVAL;
    }

    ret = wifi_uart_ensure_active();
    if (ret < 0)
    {
        MY_LOG_ERR("UART active issue ocurred: %d", ret);
        return ret;
    }

    // 发送前先刷新空闲计时，避免未来启用挂起时在等待发送完成期间被误判为空闲
    wifi_uart_activity_kick();

    // 等待上一次传输完成，增加等待超时避免异常状态下永久阻塞
    ret = k_sem_take(&s_wifi_uart_tx_done_sem, K_MSEC(WIFI_UART_TX_WAIT_MS));
    if (ret != 0)
    {
        MY_LOG_ERR("UART tx done sem issue ocurred: %d", ret);
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
        MY_LOG_WRN("UART tx issue ocurred: %d", ret);
        s_wifi_uart_ctx.tx_busy = false;
        k_sem_give(&s_wifi_uart_tx_done_sem);
        return ret;
    }

    return 0;
}

/********************************************************************
**函数名称:  my_wifi_uart_task
**入口参数:  p1       ---        保留参数
**           p2       ---        保留参数
**           p3       ---        保留参数
**出口参数:  无
**函数功能:  WIFI串口主线程，处理 UART 事件消息
**返回值:    无
*********************************************************************/
static void my_wifi_uart_task(void *p1, void *p2, void *p3)
{
    msg_t msg;
    int len;
    static uint8_t read_buf[256];

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    MY_LOG_INF("WIFI UART thread started");

    for (;;)
    {
        my_recv_msg(&my_wifi_uart_msgq, (void *)&msg, sizeof(msg_t), K_FOREVER);

        switch (msg.msgID)
        {
            case MY_MSG_UART_TX_DONE:
            case MY_MSG_UART_TX_ABORTED:
                // 发送完成后仅清 busy 状态，不额外延长空闲计时窗口
                s_wifi_uart_ctx.tx_busy = false;
                break;

            case MY_MSG_UART_IDLE:
                if (s_wifi_uart_ctx.active && wifi_uart_can_suspend())
                {
                    my_pm_device_suspend(MY_PM_DEV_WIFI);
                }
                break;

            case MY_MSG_UART_REV:
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

                    my_wifi_handle_recv(read_buf, (uint16_t)len);
                }
                break;

            case MY_MSG_UART_SEND:
                if ((msg.pData != NULL) && (msg.DataLen > 0))
                {
                    // 预留消息方式发送入口，便于后续其他模块通过消息队列投递串口发送请求
                    my_wifi_uart_send((const uint8_t *)msg.pData, (uint16_t)msg.DataLen);
                    MY_FREE_BUFFER(msg.pData);
                    msg.pData = NULL;
                }
                break;

            case MY_MSG_WIFI_PWR_ON:
                // TODO:WIFI上电 
                break;

            case MY_MSG_WIFI_PWR_OFF:
                // TODO:WIFI下电
                break;

            default:
                break;
        }
    }
}

/********************************************************************
**函数名称:  my_wifi_init
**入口参数:  tid      ---        指向线程 ID 变量的指针
**出口参数:  tid      ---        存储启动后的线程 ID
**函数功能:  初始化WIFI串口模块并启动线程
**返回值:    0 表示成功，其他表示失败
*********************************************************************/
int my_wifi_init(k_tid_t *tid)
{
    int err;
    my_uart_config_t uart_cfg;

    /* 通过 UART 抽象层统一初始化 */
    uart_cfg.dev = s_wifi_uart_dev;
    uart_cfg.cb = wifi_uart_cb;
    uart_cfg.cb_user_data = NULL;
    uart_cfg.rb = &s_wifi_uart_rb;
    uart_cfg.rb_buf = s_wifi_uart_rb_buf;
    uart_cfg.rb_size = WIFI_UART_RB_SIZE;
    uart_cfg.tx_done_sem = &s_wifi_uart_tx_done_sem;

    err = my_uart_init(&uart_cfg);
    if (err != 0)
    {
        MY_LOG_ERR("WIFI UART init failed: %d", err);
        return err;
    }

    // 初始化WIFI串口到统一PM框架，默认保持UART挂起态
    err = my_pm_device_register(MY_PM_DEV_WIFI, &s_wifi_uart_pm_ops);
    if (err < 0)
    {
        MY_LOG_ERR("WIFI UART PM registration failed: %d", err);
        return err;
    }

#if WIFI_UART_DEBUG_ENABLE
    err = my_pm_device_resume(MY_PM_DEV_WIFI);
    if (err < 0)
    {
        MY_LOG_ERR("Failed to keep WIFI UART resumed in debug mode: %d", err);
        return err;
    }
#endif
    // 初始化消息队列
    my_init_msg_handler(MOD_WIFI, &my_wifi_uart_msgq);

    // 初始化 AT 响应同步信号量
    k_sem_init(&s_wifi_at_ctx.resp_sem, 0, 1);
    s_wifi_at_ctx.state = WIFI_AT_IDLE;
    s_wifi_at_ctx.tcp_recv_cb = NULL;
    s_wifi_at_ctx.wifi_event_cb = NULL;

    *tid = k_thread_create(&s_my_wifi_uart_task_data, my_wifi_uart_task_stack,
                           K_THREAD_STACK_SIZEOF(my_wifi_uart_task_stack),
                           my_wifi_uart_task, NULL, NULL, NULL,
                           MY_WIFI_TASK_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(*tid, "MY_WIFI_UART");

    LOG_INF("WIFI UART module initialized");
    return 0;
}
