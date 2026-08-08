/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_ctrl.c
**文件描述:        系统控制模块实现文件 (LED, Motor, Key)
**当前版本:        V1.0
**作    者:        Harrison Wu (wuyujiao@jimiiot.com)
**完成日期:        2026.01.15
*********************************************************************
** 功能描述:        1. 整合 LED 与振动马达控制接口
**                 2. 实现独立线程处理按键扫描与逻辑
**                 3. 实现 FUN_KEY 按键短按/长按检测（下降沿中断+50ms轮询），实现按键事件发送到主任务
**                 4. 实现光感(light sensor)检测中断处理,消抖处理，产生有光/无光事件并发送到主任务
*********************************************************************/

/* 必须在包含 my_comm.h 之前定义 BLE_LOG_MODULE_ID，避免与 my_ble_log.h 中的默认定义冲突 */
#define BLE_LOG_MODULE_ID BLE_LOG_MOD_CTRL

#include "my_comm.h"

/* 注册控制模块日志 */
LOG_MODULE_REGISTER(my_ctrl, LOG_LEVEL_INF);

/* 循环定时器周期：50ms */
#define KEY_POLL_PERIOD_MS   50
/* 长按阈值：3秒 = 60个周期 */
#define KEY_LONG_PRESS_COUNT (3000 / KEY_POLL_PERIOD_MS)

/* 硬件设备树定义 */
static const struct gpio_dt_spec fun_key = GPIO_DT_SPEC_GET(DT_ALIAS(fun_key), gpios);
static const struct gpio_dt_spec sos_key = GPIO_DT_SPEC_GET(DT_ALIAS(sos_key), gpios);
static const struct gpio_dt_spec motor = GPIO_DT_SPEC_GET(DT_ALIAS(motor_ctrl), gpios);
static const struct gpio_dt_spec baro_pwr_en = GPIO_DT_SPEC_GET(DT_ALIAS(baro_pwr_ctrl), gpios);

/* 按键控制结构 */
static struct
{
    struct k_timer timer; /* 50ms 轮询定时器 */
    uint32_t press_count; /* 按下计数器 (50ms单位) */
    bool pressed;         /* 按键是否按下 */
} key_ctrl_t;

/* SOS按键控制结构 */
static struct
{
    struct k_timer timer; /* 50ms 轮询定时器 */
    uint32_t press_count; /* 按下计数器 (50ms单位) */
    bool pressed;         /* 按键是否按下 */
} sos_key_ctrl_t;

fs_barometer_record_t g_barometer_sample = { 0 };

/* 定时器回调前向声明 */
static void key_timer_handler(struct k_timer *timer);
static void patm_upload_timer_handler(struct k_timer *timer);

/* 消息队列定义 */
K_MSGQ_DEFINE(my_ctrl_msgq, sizeof(msg_t), 10, 4);

/* 线程数据与栈定义 */
K_THREAD_STACK_DEFINE(my_ctrl_task_stack, MY_CTRL_TASK_STACK_SIZE);
static struct k_thread s_my_ctrl_task_data;
static struct gpio_callback s_misc_io_cb;

/********************************************************************
**函数名称:  send_alarm_message_to_lte
**入口参数:  alarm_type    ---    告警类型枚举(输入)
**          additional_info   ---    附加信息字符串指针(输入，可为NULL)
**出口参数:  无
**函数功能:  发送告警消息到LTE模块
**返回值:    无
*********************************************************************/
void send_alarm_message_to_lte(alarm_type_t alarm_type, const char *additional_info)
{
    char alarm_msg[64] = {0};
    uint8_t rpt = 0;

    // 每次发送告警消息前设置开机原因
    set_lte_boot_reason(LTE_BOOT_REASON_ALARM);

    // 根据告警类型映射字符串，设置上报方式和告警类型字符串
    switch(alarm_type)
    {
        case ALARM_LOW_BAT:          // 内置电池低电报警
            break;

        case ALARM_CHARGE_IN:            // 充电器插入告警
        case ALARM_CHARGE_OUT:           // 充电器拔出告警
            rpt = gConfigParam.batlevel_config.chargesta_report;
            break;

        case ALARM_CHARGE_FULL:          // 充满状态告警
            rpt = gConfigParam.batlevel_config.chargesta_report;
            break;

        case ALARM_BAT_SWITCH:           // 电量状态切换告警
            switch(atoi(additional_info))
            {
                case BATT_EMPTY:
                    rpt = gConfigParam.batlevel_config.batlevel_empty_rpt;
                    break;

                case BATT_LOW:
                    rpt = gConfigParam.batlevel_config.batlevel_low_rpt;
                    break;

                case BATT_NORMAL:
                    rpt = gConfigParam.batlevel_config.batlevel_normal_rpt;
                    break;

                case BATT_FAIR:
                    rpt = gConfigParam.batlevel_config.batlevel_fair_rpt;
                    break;

                case BATT_HIGH:
                    rpt = gConfigParam.batlevel_config.batlevel_high_rpt;
                    break;

                case BATT_FULL:
                    rpt = gConfigParam.batlevel_config.batlevel_full_rpt;
                    break;

                default:
                    LOG_ERR("unknown BATT level");
                    break;
            }
            break;

        case ALARM_BLE_CONNECTED:        // 蓝牙连接成功告警
        case ALARM_BLE_CONNECT_ERR:      // 蓝牙连接异常告警
            rpt = gConfigParam.btconnect_config.btconnect_report;
            break;
        default:
            MY_LOG_ERR("unknown alarm type");
            return;
    }

    // 检查是否需要上报方式
    if(rpt > REPORT_MODE_NONE)
    {
        // 构建告警消息字符串
        if (additional_info != NULL && strlen(additional_info) > 0)
        {
            // 包含附加信息的格式："<告警类型>,<时间戳>,<上报方式>,<附加信息>"
            snprintf(alarm_msg, sizeof(alarm_msg), "%d,%lld,%d,%s", alarm_type, my_get_system_time_sec(), rpt, additional_info);
        }
        else
        {
            // 不包含附加信息的格式："<告警类型>,<时间戳>,<上报方式>"
            snprintf(alarm_msg, sizeof(alarm_msg), "%d,%lld,%d", alarm_type, my_get_system_time_sec(), rpt);
        }

        // 发送告警消息到LTE模块
        #if RETRANSMIT_CHECK_ENABLED
            lte_send_cmd_with_retry("ALARM", alarm_msg);
        #else
            lte_send_command("ALARM", alarm_msg);
        #endif


        // 告警唤醒4G时,根据配置的扫描模式决定是否上报扫描数据
        my_scan_upload_on_lte_wakeup();
    }
}

/********************************************************************
**函数名称:  patm_upload_timer_handler
**入口参数:  timer     ---        定时器指针（输入）
**出口参数:  无
**函数功能:  气压定时上传定时器回调，仅投递CTRL消息
**返 回 值:  无
*********************************************************************/
static void patm_upload_timer_handler(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    my_send_msg(MOD_CTRL, MOD_CTRL, MY_MSG_CTRL_PATM_TIMER);
}

/********************************************************************
**函数名称:  sensor_timer_reload
**入口参数:  timer_id     ---      定时器ID（输入）
**           interval_min ---      定时间隔，单位分钟（输入）
**           timer_fun    ---      定时器回调函数（输入）
**出口参数:  无
**函数功能:  按分钟配置重装传感器上传定时器
**返 回 值:  无
*********************************************************************/
static void sensor_timer_reload(int timer_id, uint16_t interval_min, TIMER_FUN timer_fun)
{
    uint32_t interval_ms;

    my_stop_timer(timer_id);
    if (interval_min == 0)
    {
        return;
    }

    interval_ms = (uint32_t)interval_min * 60U * 1000U;
    my_start_timer(timer_id, interval_ms, true, timer_fun);
}

/********************************************************************
**函数名称:  sensor_patm_timer_reload
**入口参数:  无
**出口参数:  无
**函数功能:  根据当前配置重装气压上传定时器
**返 回 值:  无
*********************************************************************/
static void sensor_patm_timer_reload(void)
{
    sensor_timer_reload(MY_TIMER_PATM_UPLOAD,
                        gConfigParam.patm_timer_config.interval_min,
                        (TIMER_FUN)patm_upload_timer_handler);
}

/********************************************************************
**函数名称:  sensor_sample_send_to_ble
**入口参数:  msg_id     ---        目标消息ID（输入）
**           data_ptr   ---        数据指针（输入）
**           data_len   ---        数据长度（输入）
**出口参数:  无
**函数功能:  将采样结果发送到BLE线程处理缓存与上传
**返 回 值:  无
*********************************************************************/
static void sensor_sample_send_to_ble(uint32_t msg_id, const void *data_ptr, uint32_t data_len)
{
    if (data_ptr == NULL || data_len == 0)
    {
        return;
    }

    if (msg_id == MY_MSG_BLE_SENSOR_BP_SAMPLE)
    {
        memcpy(&g_barometer_sample, data_ptr, MIN(data_len, sizeof(g_barometer_sample)));
    }

    my_send_msg(MOD_CTRL, MOD_BLE, msg_id);
}

/********************************************************************
**函数名称:  sensor_collect_barometer
**入口参数:  无
**出口参数:  无
**函数功能:  采集一次气压并执行业务处理
**返 回 值:  气压值，单位为帕斯卡（Pa）
**          失败返回-1
*********************************************************************/
static int32_t sensor_collect_barometer(void)
{
    struct barometer_data data;
    fs_barometer_record_t record;
    barometer_result_t ret;

    memset(&data, 0, sizeof(data));
    memset(&record, 0, sizeof(record));

    ret = barometer_set_work_mode(BARO_MODE_SINGLE_PRESSURE);
    if (ret != BARO_SUCCESS)
    {
        MY_LOG_ERR("barometer set mode fail:%d", ret);
        return -1;
    }

    ret = barometer_read(&data);
    if (ret != BARO_SUCCESS)
    {
        MY_LOG_ERR("barometer read fail:%d", ret);
        return -1;
    }

    record.timestamp = (uint32_t)my_get_system_time_sec();
    record.pressure_pa = (uint32_t)data.pressure_pa;
    sensor_sample_send_to_ble(MY_MSG_BLE_SENSOR_BP_SAMPLE, &record, sizeof(record));

    return record.pressure_pa;
}

/********************************************************************
**函数名称:  sensor_patm_read
**入口参数:  无
**出口参数:  无
**函数功能:  读取当前气压值并发送到BLE
**返 回 值:  无
*********************************************************************/
static void sensor_patm_read(void)
{
    float pressure_pa;
    uint8_t resp_msg[40];

    pressure_pa = sensor_collect_barometer();
    if (pressure_pa < 0)
    {
        MY_LOG_ERR("sensor_collect_barometer fail");
        send_ble_msg("NO Data!", strlen("NO Data!"));
        return;
    }
    pressure_pa /= 1000.0f;
    snprintf(resp_msg, sizeof(resp_msg), "Atmospheric Pressure %.2f Kpa", pressure_pa);
    send_ble_msg(resp_msg, strlen(resp_msg));
}

/********************************************************************
**函数名称:  device_status_read
**入口参数:  无
**出口参数:  无
**函数功能:  读取当前设备状态并发送到BLE
**返 回 值:  无
*********************************************************************/
static void device_status_read(void)
{
    char send_buf[256];
    char net_signal[10];    // 网络信号
    char gnss_signal[15];   // GNSS信号
    char pressure_str[30]; // 气压字符串
    int32_t pressure_pa = sensor_collect_barometer();

    if (pressure_pa < 0)
    {
        snprintf(pressure_str, sizeof(pressure_str), "-");
    }
    else
    {
        snprintf(pressure_str, sizeof(pressure_str), "%.2f(kPa)", pressure_pa / 1000.0f);
    }

    switch (g_lte_net_signal_level)
        {
            case 0:
                memcpy(net_signal, "NA", sizeof("NA"));
                break;

            case 1:
            case 2:
                memcpy(net_signal, "Weak", sizeof("Weak"));
                break;

            case 3:
                memcpy(net_signal, "Normal", sizeof("Normal"));
                break;

            case 4:
                memcpy(net_signal, "Strong", sizeof("Strong"));
                break;

            default:
                memcpy(net_signal, "Unknown", sizeof("Unknown"));
                break;
        }

        switch (g_lte_gps_state)
        {
            case 0:
                memcpy(gnss_signal, "OFF", sizeof("OFF"));
                break;
            case 1:
                memcpy(gnss_signal, "Searching", sizeof("Searching"));
                break;
            case 2:
                memcpy(gnss_signal, "Fix", sizeof("Fix"));
                break;
            default:
                memcpy(gnss_signal, "Unknown", sizeof("Unknown"));
                break;
        }

        snprintf(send_buf, sizeof(send_buf), "Battery:%d%%(%s);Network:%s(%s);GNSS:%s(%s);Pressure:%s",
            get_show_percent(),
            g_charg_state == NO_CHARGING ? "Discharging" : "Charging",
            g_lte_net_flag == 0 ? "Disconnect" : "Connect",
            net_signal, gnss_signal, g_lte_gps_signal,
            pressure_str);

        send_ble_msg(send_buf, strlen(send_buf));
}

/********************************************************************
**函数名称:  barometer_pwr_on
**入口参数:  on       ---        true 开启，false 关闭（输入）
**出口参数:  无
**函数功能:  控制气压传感器供电（P2.09，高电平使能）
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int barometer_pwr_on(bool on)
{
    if (!gpio_is_ready_dt(&baro_pwr_en))
    {
        return -ENODEV;
    }

    return gpio_pin_configure_dt(&baro_pwr_en, on ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
}

/********************************************************************
**函数名称:  sensor_module_init
**入口参数:  无
**出口参数:  无
**函数功能:  初始化气压与温湿度传感器模块，并按配置启动各自定时器
**返 回 值:  无
*********************************************************************/
static void sensor_module_init(void)
{
    struct barometer_config barometer_cfg;
    barometer_result_t barometer_ret;

    /* 打开气压传感器电源（P2.09） */
    if (barometer_pwr_on(true) != 0)
    {
        MY_LOG_ERR("barometer power on fail");
    }

    memset(&barometer_cfg, 0, sizeof(barometer_cfg));
    // 初始化气压传感器采样参数
    barometer_cfg.pressure_sample_rate_hz = 32;
    barometer_cfg.pressure_oversampling = 8;
    barometer_cfg.temperature_sample_rate_hz = 32;
    barometer_cfg.temperature_oversampling = 8;

    // 初始化气压传感器
    barometer_ret = barometer_init(&barometer_cfg);
    if (barometer_ret != BARO_SUCCESS)
    {
        MY_LOG_ERR("barometer init fail:%d", barometer_ret);
    }

    sensor_patm_timer_reload();
}

/********************************************************************
**函数名称:  my_ctrl_motor_on
**入口参数:  on       ---        true 开启振动，false 关闭振动（输入）
**出口参数:  无
**函数功能:  控制振动马达开关（P1.14，高电平振动）
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int my_ctrl_motor_on(bool on)
{
    if (!gpio_is_ready_dt(&motor))
    {
        return -ENODEV;
    }

    gpio_pin_set_dt(&motor, on ? 1 : 0);
    return 0;
}

/********************************************************************
**函数名称:  motor_gpio_init
**入口参数:  无
**出口参数:  无
**函数功能:  初始化振动马达 GPIO，配置为输出并默认关闭
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int motor_gpio_init(void)
{
    if (!gpio_is_ready_dt(&motor))
    {
        MY_LOG_ERR("Motor GPIO not ready");
        return -ENODEV;
    }

    return gpio_pin_configure_dt(&motor, GPIO_OUTPUT_INACTIVE);
}

/* --- 休眠唤醒功能实现 --- */

/********************************************************************
**函数名称:  peripheral_close
**入口参数:  无
**出口参数:  无
**函数功能:  低功耗模式
**返 回 值:  无
**功能描述:  1. 关闭G-Sensor
**           2. 启用充电使能(低电平为充电使能)
**           3. 关闭振动马达
*********************************************************************/
void peripheral_close()
{
    my_gsensor_pwr_on(false);
    charge_enable(true);
    my_ctrl_motor_on(false);
}

/********************************************************************
**函数名称:  enable_wakeup_pin
**入口参数:  无
**出口参数:  无
**函数功能:  配置唤醒引脚
**返 回 值:  无
**功能描述:  1. 配置 P0.4 为输入，启用内部上拉
**           2. 配置 SENSE 条件为低电平唤醒
*********************************************************************/
static void enable_wakeup_pin(void)
{
    /*  配置 P1.9 为输入，并根据外部电路选择上拉/下拉,配置 SENSE 条件，高电平唤醒*/
    nrf_gpio_cfg_sense_input(NRF_GPIO_PIN_MAP(1, 9), NRF_GPIO_PIN_PULLDOWN, NRF_GPIO_PIN_SENSE_HIGH);
    /* 配置 P0.3 为输入，并根据外部电路选择上拉/下拉,配置 SENSE 条件，低电平唤醒*/
    nrf_gpio_cfg_sense_input(NRF_GPIO_PIN_MAP(0, 3), NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
}

/********************************************************************
**函数名称:  go_to_system_off
**入口参数:  无
**出口参数:  无
**函数功能:  进入系统深度休眠
**返 回 值:  无
**功能描述:  1. 清除 RESETREAS 避免立即唤醒
**           2. 配置唤醒引脚
**           3. 延迟 2 秒确保日志输出
**           4. 进入 System OFF 模式
*********************************************************************/
void go_to_system_off(void)
{
    MY_LOG_INF("Config wakeup pin and enter System OFF");


    k_sleep(K_SECONDS(2));// 确保上面的日志有打印出来

    peripheral_close();

    k_msleep(10); // 确保电平稳定

    /* 清 RESETREAS，避免立即被旧的唤醒原因拉起（手册要求） */
    nrf_reset_resetreas_clear(NRF_RESET, 0xFFFFFFFF);

    enable_wakeup_pin();

    // 如果设备在引脚复位和上电复位后过早进入 System OFF 模式可能会造成电源异常，写入此寄存器为修复措施
    *(volatile uint32_t *) 0x5005340C = 1;

    // 关闭 GRTC 和 LF 时钟
    sys_clock_disable();

    // 关闭不需要的 RAM 段
    NRF_MEMCONF->POWER[1].RET &= ~0xE;

    /* 进入 System OFF（深度睡眠） */
    sys_poweroff();
}

/********************************************************************
**函数名称:  key_timer_init
**入口参数:  无
**出口参数:  无
**函数功能:  初始化按键检测定时器
**返 回 值:  无
**功能描述:  1. 初始化50ms轮询定时器
**           2. 复位按键状态和计数器
*********************************************************************/
static void key_timer_init(void)
{
    k_timer_init(&key_ctrl_t.timer, key_timer_handler, NULL);
    key_ctrl_t.pressed = false;
    key_ctrl_t.press_count = 0;
}

/********************************************************************
**函数名称:  send_key_event
**入口参数:  msg_id   ---   消息ID (短按/长按)
**出口参数:  无
**函数功能:  发送按键事件到主任务
**返 回 值:  无
**功能描述:  封装按键事件消息并通过my_send_msg_data发送到MAIN模块
*********************************************************************/
static void send_key_event(uint32_t msg_id)
{
    msg_t msg;
    msg.msgID = msg_id;
    msg.pData = NULL;
    msg.DataLen = 0;
    my_send_msg_data(MOD_CTRL, MOD_MAIN, &msg);
}

/********************************************************************
**函数名称:  key_timer_handler
**入口参数:  timer    ---   定时器指针
**出口参数:  无
**函数功能:  50ms轮询定时器回调，检测按键状态
**返 回 值:  无
**功能描述:  1. 每50ms读取按键电平
**           2. 按键按下时计数器累加，达到60次(3s)触发长按事件
**           3. 按键释放时停止定时器，根据计数判断短按(>=100ms)并发送事件
*********************************************************************/
static void key_timer_handler(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    int level = gpio_pin_get(fun_key.port, fun_key.pin);

    if (level == 1)
    {
        /* 按键持续按下 */
        if (!key_ctrl_t.pressed)
        {
            key_ctrl_t.pressed = true;
            key_ctrl_t.press_count = 0;
        }

        /* 计数器增加 */
        key_ctrl_t.press_count++;
    }
    else
    {
        /* 按键已释放 */
        if (key_ctrl_t.pressed)
        {
            key_ctrl_t.pressed = false;
            k_timer_stop(&key_ctrl_t.timer);

            /* 短按判断：大于等于100ms且小于3s */
            if (key_ctrl_t.press_count < KEY_LONG_PRESS_COUNT && key_ctrl_t.press_count >= 2)
            {
                send_key_event(MY_MSG_CTRL_KEY_SHORT_PRESS);
            }
            else if (key_ctrl_t.press_count >= KEY_LONG_PRESS_COUNT)
            {
                send_key_event(MY_MSG_CTRL_KEY_LONG_PRESS);
            }
            key_ctrl_t.press_count = 0;
        }
    }
}

/********************************************************************
**函数名称:  sos_key_timer_handler
**入口参数:  timer    ---   定时器指针
**出口参数:  无
**函数功能:  50ms轮询定时器回调，检测SOS按键状态
**返 回 值:  无
**功能描述:  1. 每50ms读取SOS按键电平
**           2. 按键按下时计数器累加，达到60次(3s)触发长按事件
**           3. 按键释放时停止定时器，根据计数判断短按并发送事件到主任务
*********************************************************************/
static void sos_key_timer_handler(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    int level = gpio_pin_get(sos_key.port, sos_key.pin);

    if (level == 1)
    {
        /* 按键持续按下 */
        if (!sos_key_ctrl_t.pressed)
        {
            sos_key_ctrl_t.pressed = true;
            sos_key_ctrl_t.press_count = 0;
        }

        /* 计数器增加 */
        sos_key_ctrl_t.press_count++;
    }
    else
    {
        /* 按键已释放 */
        if (sos_key_ctrl_t.pressed)
        {
            sos_key_ctrl_t.pressed = false;
            k_timer_stop(&sos_key_ctrl_t.timer);

            /* 短按判断：大于等于100ms且小于3s */
            if (sos_key_ctrl_t.press_count < KEY_LONG_PRESS_COUNT && sos_key_ctrl_t.press_count >= 2)
            {
                send_key_event(MY_MSG_CTRL_SOS_SHORT_PRESS);
            }
            else if (sos_key_ctrl_t.press_count >= KEY_LONG_PRESS_COUNT)
            {
                send_key_event(MY_MSG_CTRL_SOS_LONG_PRESS);
            }
            sos_key_ctrl_t.press_count = 0;
        }
    }
}

/********************************************************************
**函数名称:  misc_io_isr
**入口参数:  dev      ---   GPIO 设备指针
**           cb       ---   回调结构体指针
**           pins     ---   触发中断的引脚位图
**出口参数:  无
**函数功能:  杂项 IO 中断服务程序，处理 FUN_KEY 按键中断
**返 回 值:  无
*********************************************************************/
static void misc_io_isr(const struct device *dev,
                   struct gpio_callback *cb,
                   uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);

    if (pins & BIT(fun_key.pin))
    {
        if (!k_timer_remaining_get(&key_ctrl_t.timer))
        {
            //定时器不在运行就启动定时器
            k_timer_start(&key_ctrl_t.timer, K_MSEC(KEY_POLL_PERIOD_MS), K_MSEC(KEY_POLL_PERIOD_MS));
        }
    }

    if (pins & BIT(sos_key.pin))
    {
        if (!k_timer_remaining_get(&sos_key_ctrl_t.timer))
        {
            //定时器不在运行就启动定时器
            k_timer_start(&sos_key_ctrl_t.timer, K_MSEC(KEY_POLL_PERIOD_MS), K_MSEC(KEY_POLL_PERIOD_MS));
        }
    }
}

/********************************************************************
**函数名称:  misc_io_init
**入口参数:  无
**出口参数:  无
**函数功能:  初始化 FUN_KEY 按键 IO
**返 回 值:  0 表示成功，负值表示失败
**功能描述:  1. 检查 GPIO 设备就绪状态
**           2. 配置 fun_key 为输入（内部下拉）
**           3. 配置按键中断触发
**           4. 初始化按键定时器并注册中断回调
*********************************************************************/
static int misc_io_init(void)
{
    int ret;
    uint32_t fun_key_count = 0;

    if (!device_is_ready(fun_key.port))
    {
        return -ENODEV;
    }

    /* 配置为输入（fun_key 配置内部下拉） */
    ret = gpio_pin_configure(fun_key.port, fun_key.pin, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret)
    {
        MY_LOG_ERR("Failed to configure fun_key: %d", ret);
        return ret;
    }

    if (get_charge_state_level() == 0)
    {
        while (gpio_pin_get(fun_key.port, fun_key.pin))
        {
            fun_key_count++;
            k_msleep(KEY_POLL_PERIOD_MS);
        }

        if (fun_key_count < KEY_LONG_PRESS_COUNT)
        {
            go_to_system_off();
        }
    }

    /* 配置按键中断：上升沿触发 */
    ret = gpio_pin_interrupt_configure_dt(&fun_key, GPIO_INT_EDGE_RISING);
    if (ret)
    {
        MY_LOG_ERR("Failed to configure fun_key interrupt: %d", ret);
        return ret;
    }

    /* 初始化按键定时器 */
    key_timer_init();

    /* 配置SOS按键为输入（P1.10 内部下拉） */
    if (!device_is_ready(sos_key.port))
    {
        return -ENODEV;
    }

    ret = gpio_pin_configure(sos_key.port, sos_key.pin, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret)
    {
        MY_LOG_ERR("Failed to configure sos_key: %d", ret);
        return ret;
    }

    /* 配置SOS按键中断：上升沿触发 */
    ret = gpio_pin_interrupt_configure_dt(&sos_key, GPIO_INT_EDGE_RISING);
    if (ret)
    {
        MY_LOG_ERR("Failed to configure sos_key interrupt: %d", ret);
        return ret;
    }

    /* 初始化SOS按键定时器 */
    k_timer_init(&sos_key_ctrl_t.timer, sos_key_timer_handler, NULL);

    /* 注册按键中断回调（fun_key 与 sos_key 共用同一 GPIO 端口 gpio1） */
    gpio_init_callback(&s_misc_io_cb, misc_io_isr, BIT(fun_key.pin) | BIT(sos_key.pin));
    gpio_add_callback(fun_key.port, &s_misc_io_cb);

    return 0;
}

/* 电量LED功能已删除：P2.07/P2.08/P2.09 改作充电使能/WIFI电源/气压计电源 */
int batt_led_set_level(uint8_t level)
{
    ARG_UNUSED(level);

    /* 电量LED硬件已删除，保留空实现以兼容旧调用 */
    return 0;
}

/********************************************************************
**函数名称:  my_ctrl_task
**入口参数:  p1, p2, p3   ---   线程参数（未使用）
**出口参数:  无
**函数功能:  控制模块主线程
**返 回 值:  无
**功能描述:  1. 循环接收消息队列消息
**           2. 根据消息 ID 分发处理不同事件
**           3. 处理按键短按/长按事件等
*********************************************************************/
static void my_ctrl_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    msg_t msg;

    if (gConfigParam.led_config.led_display == 2)
    {
        my_send_msg(MOD_CTRL, MOD_CTRL, MY_MSG_LED_ENABLE);
    }
    else
    {
        my_send_msg(MOD_CTRL, MOD_CTRL, MY_MSG_LED_DISABLE);
    }

    MY_LOG_INF("Control thread started");

    k_msleep(100); // 等待100ms，确保BLE线程启动发送蓝牙广播消息

    for (;;)
    {
        my_recv_msg(&my_ctrl_msgq, (void *)&msg, sizeof(msg_t), K_FOREVER);

        switch (msg.msgID)
        {
            case MY_MSG_SHOW_CHARG:
                my_battery_show_chgled();//显示充电状态LED
                break;

            case MY_MSG_UPDATE_BATTERY:
                my_battery_update_state();//更新电池状态
                break;

            case MY_MSG_CTRL_PATM_TIMER:
                sensor_collect_barometer();
                break;

            case MY_MSG_CTRL_PATM_RELOAD:
                sensor_patm_timer_reload();
                break;

            case MY_MSG_CTRL_PATM_READ:
                sensor_patm_read();
                break;

            case MY_MSG_CTRL_STATUS_READ:
                device_status_read();
                break;

            case MY_MSG_LED_CTRL_MODE:
                my_led_ctrl_mode();
                break;

            case MY_MSG_LED_ENABLE:
                if (gConfigParam.led_config.led_display == 2)
                {
                    my_stop_timer(MY_TIMER_LED_ENABLE);
                }
                led_enable(true);
                my_send_msg(MOD_CTRL, MOD_CTRL, MY_MSG_LED_CTRL_MODE);
                break;

            case MY_MSG_LED_DISABLE:
                my_stop_timer(MY_TIMER_LED_BLINK);
                led_enable(false);
                break;

            default:
                break;
        }
    }
}

/********************************************************************
**函数名称:  my_ctrl_init
**入口参数:  tid      ---   指向线程 ID 变量的指针
**出口参数:  tid      ---   存储启动后的线程 ID
**函数功能:  初始化控制模块并启动控制线程
**返 回 值:  0 表示成功，负值表示失败
**功能描述:  1. 初始化按键、光感 IO 中断
**           2. 初始化 LED GPIO
**           3. 初始化电池 ADC GPIO
**           4. 初始化振动马达 GPIO
**           5. 初始化消息队列处理
**           6. 启动控制线程并设置名称
**           7. 播放启动提示音
*********************************************************************/
int my_ctrl_init(k_tid_t *tid)
{
    int ret;

    // 初始化消息队列
    my_init_msg_handler(MOD_CTRL, &my_ctrl_msgq);

    //  初始化按键、电池 GPIO
    batt_gpio_init();
    misc_io_init();
    // 注：初始化中会立即开启定时器触发batt_update_timer_handler回调，会向ctrl发送消息（由于未初始化会丢消息），需放在ctrl初始化之后
    batt_adc_init();

    // 初始化振动马达 GPIO（P1.14，默认关闭）
    motor_gpio_init();

    ret = my_battery_pm_register();
    if (ret < 0)
    {
        MY_LOG_ERR("Battery PM registration failed: %d", ret);
        return ret;
    }

    sensor_module_init();

    // 启动控制线程
    *tid = k_thread_create(&s_my_ctrl_task_data, my_ctrl_task_stack,
                           K_THREAD_STACK_SIZEOF(my_ctrl_task_stack),
                           my_ctrl_task, NULL, NULL, NULL,
                           MY_CTRL_TASK_PRIORITY, 0, K_NO_WAIT);

    // 设置线程名称
    k_thread_name_set(*tid, "MY_CTRL");

    MY_LOG_INF("Control module initialized");
    return 0;
}
