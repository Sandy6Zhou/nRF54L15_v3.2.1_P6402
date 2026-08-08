#define BLE_LOG_MODULE_ID BLE_LOG_MOD_SENSOR

#include "my_comm.h"
#include "imu_api.h"

LOG_MODULE_REGISTER(my_gsensor, LOG_LEVEL_INF);

static const struct gpio_dt_spec s_gsensor_pwr_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(gsensor_pwr_ctrl), gpios);
static uint8_t s_chip_id = 0;                       // 芯片ID缓存，初始化时读取

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
**函数名称:  my_gsensor_init
**入口参数:  tid      ---        任务 ID 存储地址
**出口参数:  tid      ---        未创建任务时写入 NULL
**函数功能:  初始化 QMI8658B 六轴传感器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int my_gsensor_init(k_tid_t *tid)
{
    int ret;

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

    if (tid != NULL)
    {
        *tid = NULL;
    }

    return 0;
}

/********************************************************************
**函数名称:  my_gsensor_read_data
**入口参数:  无
**出口参数:  data     ---        六轴换算数据
**函数功能:  读取 QMI8658B 当前六轴数据
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
