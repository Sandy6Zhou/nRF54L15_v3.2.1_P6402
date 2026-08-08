#define BLE_LOG_MODULE_ID BLE_LOG_MOD_SENSOR

#include "my_comm.h"
#include "imu_api.h"

LOG_MODULE_REGISTER(my_gsensor, LOG_LEVEL_INF);

static const struct gpio_dt_spec s_gsensor_pwr_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(gsensor_pwr_ctrl), gpios);
static uint8_t s_chip_id = 0;                       // 芯片ID缓存，初始化时读取

/* gsensor 线程内缓存数据，外部通过 get 接口查询最近一次读取结果 */
static my_gsensor_data_t s_gsensor_data = { 0 };
static battery_gauge_data_t s_gauge_data = { 0 };

K_MSGQ_DEFINE(my_gsensor_msgq, sizeof(msg_t), 10, 4);
K_THREAD_STACK_DEFINE(my_gsensor_task_stack, MY_GSENSOR_TASK_STACK_SIZE);
static struct k_thread s_my_gsensor_task_data;

/********************************************************************
**函数名称:  gsensor_int_callback
**入口参数:  无
**出口参数:  无
**函数功能:  QMI8658B INT1 中断回调，中断上下文仅投递消息
**返回值:    无
**注意事项:  禁止在中断上下文直接访问 I2C，仅投递消息由线程处理
*********************************************************************/
static void gsensor_int_callback(void)
{
    my_send_msg(MOD_GSENSOR, MOD_GSENSOR, MY_MSG_GSENSOR_INT);
}

/********************************************************************
**函数名称:  gauge_int_callback
**入口参数:  无
**出口参数:  无
**函数功能:  OM70201WV INTN 中断回调，中断上下文仅投递消息
**返回值:    无
**注意事项:  禁止在中断上下文直接访问 I2C，仅投递消息由线程处理
*********************************************************************/
static void gauge_int_callback(void)
{
    my_send_msg(MOD_GSENSOR, MOD_GSENSOR, MY_MSG_READ_GAUGE_DATA);
}

/********************************************************************
**函数名称:  my_gsensor_pwr_on
**入口参数:  on       ---        true 开启，false 关闭
**出口参数:  无
**函数功能:  控制六轴传感器供电
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int my_gsensor_pwr_on(bool on)
{
    if (!gpio_is_ready_dt(&s_gsensor_pwr_gpio))
    {
        return -ENODEV;
    }

    return gpio_pin_configure_dt(&s_gsensor_pwr_gpio, on ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
}

/********************************************************************
**函数名称:  my_gsensor_read_data
**入口参数:  无
**出口参数:  data     ---        六轴换算数据
**函数功能:  同步读取 QMI8658B 当前六轴数据
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int my_gsensor_read_data(my_gsensor_data_t *data)
{
    imu_data_t imu_data;
    int ret;

    if (data == NULL)
    {
        return -EINVAL;
    }

    ret = imu_read(&imu_data);
    if (ret != IMU_SUCCESS)
    {
        return -EIO;
    }

    data->acc_x_mg = imu_data.acc_x;
    data->acc_y_mg = imu_data.acc_y;
    data->acc_z_mg = imu_data.acc_z;
    data->gyr_x_mdps = imu_data.gyr_x;
    data->gyr_y_mdps = imu_data.gyr_y;
    data->gyr_z_mdps = imu_data.gyr_z;

    return 0;
}

/********************************************************************
**函数名称:  my_gsensor_get_data
**入口参数:  无
**出口参数:  data     ---        gsensor 线程缓存的六轴数据
**函数功能:  获取 gsensor 线程最近一次读取的六轴数据
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int my_gsensor_get_data(my_gsensor_data_t *data)
{
    if (data == NULL)
    {
        return -EINVAL;
    }

    *data = s_gsensor_data;

    return 0;
}

/********************************************************************
**函数名称:  my_gsensor_get_gauge
**入口参数:  无
**出口参数:  data     ---        gsensor 线程缓存的库仑计数据
**函数功能:  获取 gsensor 线程最近一次读取的库仑计电量数据
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int my_gsensor_get_gauge(battery_gauge_data_t *data)
{
    if (data == NULL)
    {
        return -EINVAL;
    }

    *data = s_gauge_data;

    return 0;
}

/********************************************************************
**函数名称:  my_gsensor_request_read
**入口参数:  无
**出口参数:  无
**函数功能:  投递读取六轴数据消息到 gsensor 线程
**返回值:    0 表示投递成功，负值表示失败
*********************************************************************/
int my_gsensor_request_read(void)
{
    msg_t msg;

    memset(&msg, 0, sizeof(msg_t));
    msg.msgID = MY_MSG_READ_GSENSOR_DATA;

    my_send_msg_data(MOD_GSENSOR, MOD_GSENSOR, &msg);

    return 0;
}

/********************************************************************
**函数名称:  my_gsensor_request_gauge
**入口参数:  无
**出口参数:  无
**函数功能:  投递读取库仑计电量数据消息到 gsensor 线程
**返回值:    0 表示投递成功，负值表示失败
*********************************************************************/
int my_gsensor_request_gauge(void)
{
    msg_t msg;

    memset(&msg, 0, sizeof(msg_t));
    msg.msgID = MY_MSG_READ_GAUGE_DATA;

    my_send_msg_data(MOD_GSENSOR, MOD_GSENSOR, &msg);

    return 0;
}

/********************************************************************
**函数名称:  get_chip_id
**入口参数:  无
**出口参数:  无
**函数功能:  获取 QMI8658B 芯片ID
**返 回 值:  QMI8658B 芯片ID
*********************************************************************/
uint8_t get_chip_id(void)
{
    return s_chip_id;
}

/********************************************************************
**函数名称:  my_gsensor_task
**入口参数:  p1       ---        保留参数
**           p2       ---        保留参数
**           p3       ---        保留参数
**出口参数:  无
**函数功能:  gsensor 主线程，串行处理六轴与库仑计数据读取消息
**返回值:    无
**注意事项:  六轴与库仑计共用 i2c21，两条读取消息在同一线程内串行执行
*********************************************************************/
static void my_gsensor_task(void *p1, void *p2, void *p3)
{
    msg_t msg;
    uint8_t int_status;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    MY_LOG_INF("G-Sensor thread started");

    for (;;)
    {
        my_recv_msg(&my_gsensor_msgq, (void *)&msg, sizeof(msg_t), K_FOREVER);

        switch (msg.msgID)
        {
            case MY_MSG_READ_GSENSOR_DATA:
                // 串行读取六轴数据并缓存到全局，供 get 接口查询
                my_gsensor_read_data(&s_gsensor_data);
                break;

            case MY_MSG_READ_GAUGE_DATA:
                // 串行读取库仑计电量数据并缓存到全局，供 get 接口查询
                battery_gauge_read(&s_gauge_data);
                break;

            case MY_MSG_GSENSOR_INT:
                // 六轴 INT1 中断触发：读取中断状态寄存器以清除中断标志
                imu_read_int_status(&int_status);
                break;

            default:
                break;
        }
    }
}

/********************************************************************
**函数名称:  my_gsensor_init
**入口参数:  tid      ---        任务 ID 存储地址
**出口参数:  tid      ---        创建线程后写入线程 ID
**函数功能:  初始化 QMI8658B 六轴传感器与 OM70201WV 库仑计，
**           创建 gsensor 线程，串行处理六轴/库仑计数据读取消息
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int my_gsensor_init(k_tid_t *tid)
{
    int ret;

    if (tid == NULL)
    {
        return -EINVAL;
    }

    ret = my_gsensor_pwr_on(true);
    if (ret != 0)
    {
        return ret;
    }

    k_msleep(10);

    ret = imu_init(NULL);
    if (ret != IMU_SUCCESS)
    {
        return -EIO;
    }

    /* 缓存芯片ID，供 get_chip_id 查询 */
    imu_get_chip_id(&s_chip_id);

    /* 注册 QMI8658B INT1 中断回调（P1.02） */
    imu_register_int_callback(gsensor_int_callback);

    /* 初始化 OM70201WV 库仑计，失败不阻塞六轴功能 */
    ret = battery_gauge_init(NULL);
    if (ret != BATTERY_GAUGE_SUCCESS)
    {
        MY_LOG_ERR("Battery gauge init failed: %d", ret);
    }

    /* 注册 OM70201WV INTN 中断回调（P1.11） */
    battery_gauge_register_interrupt_callback(gauge_int_callback);

    /* 注册消息队列并创建 gsensor 线程 */
    my_init_msg_handler(MOD_GSENSOR, &my_gsensor_msgq);

    *tid = k_thread_create(&s_my_gsensor_task_data, my_gsensor_task_stack,
                            K_THREAD_STACK_SIZEOF(my_gsensor_task_stack),
                            my_gsensor_task, NULL, NULL, NULL,
                            MY_GSENSOR_TASK_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(*tid, "MY_GSENSOR");

    return 0;
}
