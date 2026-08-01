/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        imu_api.c
**文件描述:        QMI8658B 六轴传感器统一接口实现文件
**当前版本:        V1.0
**作    者:        周森达 (zhousenda@jimiiot.com)
**完成日期:        2026.07.30
*********************************************************************
** 功能描述:        封装 QMI8658B 芯片驱动和 Zephyr 端口层
**                 提供初始化、数据读取、FIFO、中断、特性、校准和自检统一接口
*********************************************************************/

#include "imu_api.h"
#include "../driver/qmi8658b_driver.h"
#include "../driver/qmi8658b_reg.h"
#include "../port/qmi8658b_port.h"

#include <errno.h>
#include <string.h>

#define IMU_SENSOR_ACC                  0x01U   /* 加速度计使能位 */
#define IMU_SENSOR_GYR                  0x02U   /* 陀螺仪使能位 */
#define IMU_FIFO_FRAME_BYTES_6DOF       12U     /* 六轴 FIFO 单帧字节数 */
#define IMU_FIFO_FRAME_BYTES_3DOF       6U      /* 单轴 FIFO 单帧字节数 */
#define IMU_FIFO_MAX_FRAMES             128U    /* FIFO 配置为 128 样本时的最大帧数 */

static qmi8658b_driver_t s_driver;
static imu_config_t s_config;
static bool s_initialized;
static uint8_t s_fifo_buffer[IMU_FIFO_MAX_FRAMES * IMU_FIFO_FRAME_BYTES_6DOF];

/********************************************************************
**函数名称:  imu_convert_result
**入口参数:  ret      ---        底层错误码（输入）
**出口参数:  无
**函数功能:  转换底层错误码为通用接口错误码
**返回值:    对应通用接口错误码
*********************************************************************/
static imu_result_t imu_convert_result(int ret)
{
    if (ret == 0)
    {
        return IMU_SUCCESS;
    }

    if (ret == -ENODEV)
    {
        return IMU_ERROR_CHIP_ID;
    }

    if (ret == -EINVAL)
    {
        return IMU_ERROR_PARAM;
    }

    if (ret == -ETIMEDOUT)
    {
        return IMU_ERROR_TIMEOUT;
    }

    if (ret == -ENOTSUP)
    {
        return IMU_ERROR_NOT_SUPPORTED;
    }

    if (ret == -EBUSY)
    {
        return IMU_ERROR_NOT_SUPPORTED;
    }

    return IMU_ERROR_COMM;
}

/********************************************************************
**函数名称:  imu_check_initialized
**入口参数:  无
**出口参数:  无
**函数功能:  检查通用接口初始化状态
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
static imu_result_t imu_check_initialized(void)
{
    return s_initialized ? IMU_SUCCESS : IMU_ERROR_INIT;
}

/********************************************************************
**函数名称:  imu_map_acc_range
**入口参数:  range    ---        通用加速度量程（输入）
**出口参数:  value    ---        芯片使用的加速度量程设置值
**函数功能:  转换加速度量程设置
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int imu_map_acc_range(imu_acc_range_t range, uint8_t *value)
{
    if ((value == NULL) || (range >= IMU_ACC_RANGE_MAX))
    {
        return -EINVAL;
    }

    *value = (uint8_t)((uint8_t)range << 4);  // 加速度计量程在 CTRL2 bits[6:4]
    return 0;
}

/********************************************************************
**函数名称:  imu_map_gyr_range
**入口参数:  range    ---        通用陀螺仪量程（输入）
**出口参数:  value    ---        芯片使用的陀螺仪量程设置值
**函数功能:  转换陀螺仪量程设置
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int imu_map_gyr_range(imu_gyr_range_t range, uint8_t *value)
{
    if ((value == NULL) || (range >= IMU_GYR_RANGE_MAX))
    {
        return -EINVAL;
    }

    *value = (uint8_t)((uint8_t)range << 4);  // 陀螺仪量程在 CTRL3 bits[6:4]
    return 0;
}

/********************************************************************
**函数名称:  imu_map_acc_odr
**入口参数:  odr      ---        加速度计输出速率（输入）
**出口参数:  value    ---        芯片使用的 aODR 寄存器值
**函数功能:  验证并返回加速度计 ODR 寄存器值
**返回值:    0 表示成功，负值表示失败
**注意事项:  ODR 枚举值直接对应 CTRL2/CTRL3 寄存器位值
**           低功耗 ODR(0x0C~0x0F)仅加速度计可用，需陀螺仪关闭
**           0x00~0x02 仅 6DOF 模式下有效
*********************************************************************/
static int imu_map_acc_odr(imu_odr_t odr, uint8_t *value)
{
    if (value == NULL)
    {
        return -EINVAL;
    }

    switch (odr)
    {
        case IMU_ODR_7174HZ:    /* 0x00, 6DOF only */
        case IMU_ODR_3587HZ:    /* 0x01, 6DOF only */
        case IMU_ODR_1793HZ:    /* 0x02, 6DOF only */
        case IMU_ODR_1000HZ:    /* 0x03 */
        case IMU_ODR_500HZ:     /* 0x04 */
        case IMU_ODR_250HZ:     /* 0x05 */
        case IMU_ODR_125HZ:     /* 0x06 */
        case IMU_ODR_62_5HZ:    /* 0x07 */
        case IMU_ODR_31_25HZ:   /* 0x08 */
        case IMU_ODR_128HZ_LP:  /* 0x0C, accel only low power */
        case IMU_ODR_21HZ_LP:   /* 0x0D, accel only low power */
        case IMU_ODR_11HZ_LP:   /* 0x0E, accel only low power */
        case IMU_ODR_3HZ_LP:    /* 0x0F, accel only low power */
            *value = (uint8_t)odr;
            return 0;

        default:
            return -EINVAL;
    }
}

/********************************************************************
**函数名称:  imu_map_gyr_odr
**入口参数:  odr      ---        陀螺仪输出速率（输入）
**出口参数:  value    ---        芯片使用的 gODR 寄存器值
**函数功能:  验证并返回陀螺仪 ODR 寄存器值
**返回值:    0 表示成功，负值表示失败
**注意事项:  陀螺仪仅支持 0x00~0x08，低功耗 ODR 不可用于陀螺仪
*********************************************************************/
static int imu_map_gyr_odr(imu_odr_t odr, uint8_t *value)
{
    if (value == NULL)
    {
        return -EINVAL;
    }

    switch (odr)
    {
        case IMU_ODR_7174HZ:    /* 0x00 */
        case IMU_ODR_3587HZ:    /* 0x01 */
        case IMU_ODR_1793HZ:    /* 0x02 */
        case IMU_ODR_1000HZ:    /* 0x03 */
        case IMU_ODR_500HZ:     /* 0x04 */
        case IMU_ODR_250HZ:     /* 0x05 */
        case IMU_ODR_125HZ:     /* 0x06 */
        case IMU_ODR_62_5HZ:    /* 0x07 */
        case IMU_ODR_31_25HZ:   /* 0x08 */
            *value = (uint8_t)odr;
            return 0;

        default:
            // 低功耗 ODR (0x0C~0x0F) 和保留值 (0x09~0x0B) 均不支持陀螺仪
            return -EINVAL;
    }
}

/********************************************************************
**函数名称:  imu_is_acc_lp_odr
**入口参数:  odr      ---        ODR 枚举值（输入）
**出口参数:  无
**函数功能:  判断 ODR 是否为加速度计低功耗模式
**返回值:    true 表示低功耗模式
*********************************************************************/
static bool imu_is_acc_lp_odr(imu_odr_t odr)
{
    return (odr == IMU_ODR_128HZ_LP) || (odr == IMU_ODR_21HZ_LP) ||
           (odr == IMU_ODR_11HZ_LP) || (odr == IMU_ODR_3HZ_LP);
}

/********************************************************************
**函数名称:  imu_is_6dof_only_odr
**入口参数:  odr      ---        ODR 枚举值（输入）
**出口参数:  无
**函数功能:  判断 ODR 是否仅在 6DOF 模式下有效
**返回值:    true 表示仅 6DOF 有效
*********************************************************************/
static bool imu_is_6dof_only_odr(imu_odr_t odr)
{
    return (odr == IMU_ODR_7174HZ) || (odr == IMU_ODR_3587HZ) || (odr == IMU_ODR_1793HZ);
}

/********************************************************************
**函数名称:  imu_make_driver_config
**入口参数:  config   ---        通用配置（输入）
**出口参数:  driver_config ---   芯片配置（输出）
**函数功能:  将通用配置转换为芯片配置，含 ODR 有效性校验
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int imu_make_driver_config(const imu_config_t *config, qmi8658b_config_t *driver_config)
{
    uint8_t sensor_enable;
    int ret;

    if ((config == NULL) || (driver_config == NULL) || (config->power_mode >= IMU_POWER_MAX))
    {
        return -EINVAL;
    }

    if (config->lpf_mode >= IMU_LPF_MODE_MAX)
    {
        return -EINVAL;
    }

    memset(driver_config, 0, sizeof(*driver_config));
    ret = imu_map_acc_range(config->acc_range, &driver_config->acc_range);
    if (ret != 0)
    {
        return ret;
    }

    ret = imu_map_gyr_range(config->gyr_range, &driver_config->gyr_range);
    if (ret != 0)
    {
        return ret;
    }

    ret = imu_map_acc_odr(config->acc_odr, &driver_config->acc_odr);
    if (ret != 0)
    {
        return ret;
    }

    ret = imu_map_gyr_odr(config->gyr_odr, &driver_config->gyr_odr);
    if (ret != 0)
    {
        return ret;
    }

    // 根据电源模式确定传感器使能位
    switch (config->power_mode)
    {
        case IMU_POWER_DOWN:
            sensor_enable = 0U;
            break;

        case IMU_POWER_SUSPEND:
            sensor_enable = 0U;
            break;

        case IMU_POWER_LOW_POWER:
            // 低功耗模式: 仅加速度计，需验证 ODR 为低功耗设置
            if (!imu_is_acc_lp_odr(config->acc_odr))
            {
                return -EINVAL;
            }

            sensor_enable = IMU_SENSOR_ACC;
            break;

        case IMU_POWER_GYRO_SNOOZE:
            // 陀螺仪休眠: 加速度计正常，陀螺仪仅保持驱动
            sensor_enable = IMU_SENSOR_ACC | IMU_SENSOR_GYR;
            break;

        case IMU_POWER_NORMAL:
        default:
            // 正常模式: 加速度计和陀螺仪均工作
            // 验证正常模式的 ODR 兼容性
            if (imu_is_acc_lp_odr(config->acc_odr))
            {
                return -EINVAL;
            }

            sensor_enable = IMU_SENSOR_ACC | IMU_SENSOR_GYR;
            break;
    }

    // 6DOF 专用 ODR 校验: 0x00~0x02 仅在双传感器同时使能时有效
    if ((imu_is_6dof_only_odr(config->acc_odr) || imu_is_6dof_only_odr(config->gyr_odr)) &&
        (sensor_enable != (IMU_SENSOR_ACC | IMU_SENSOR_GYR)))
    {
        return -EINVAL;
    }

    driver_config->sensor_enable = sensor_enable;
    driver_config->ctrl8_value = 0xC0U;       // CTRL9握手经STATUSINT.bit7(bit7=1), 活动检测→INT1(bit6=1)
    driver_config->lpf_enable = config->lpf_enable;
    driver_config->lpf_mode = (uint8_t)config->lpf_mode;

    return 0;
}

/********************************************************************
**函数名称:  imu_fill_default_config
**入口参数:  config   ---        通用配置（输出）
**出口参数:  config   ---        默认配置
**函数功能:  填充默认六轴工作配置
**返回值:    无
*********************************************************************/
static void imu_fill_default_config(imu_config_t *config)
{
    config->acc_range = IMU_ACC_RANGE_8G;
    config->acc_odr = IMU_ODR_125HZ;
    config->gyr_range = IMU_GYR_RANGE_1024DPS;
    config->gyr_odr = IMU_ODR_125HZ;
    config->power_mode = IMU_POWER_NORMAL;
    config->lpf_enable = true;
    config->lpf_mode = IMU_LPF_MODE_0;
}

/********************************************************************
**函数名称:  imu_init
**入口参数:  config   ---        初始化配置，NULL 使用默认值（输入）
**出口参数:  无
**函数功能:  初始化通用六轴接口
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_init(const imu_config_t *config)
{
    const qmi8658b_bus_t *bus;
    qmi8658b_config_t driver_config;
    imu_config_t local_config;
    int ret;

    if (config == NULL)
    {
        imu_fill_default_config(&local_config);
        config = &local_config;
    }

    ret = imu_make_driver_config(config, &driver_config);
    if (ret != 0)
    {
        return imu_convert_result(ret);
    }

    ret = qmi8658b_port_init();
    if (ret != 0)
    {
        return imu_convert_result(ret);
    }

    bus = qmi8658b_port_get_bus();
    if (bus == NULL)
    {
        return IMU_ERROR_INIT;
    }

    ret = qmi8658b_driver_init(&s_driver, bus, &driver_config);
    if (ret != 0)
    {
        return imu_convert_result(ret);
    }

    s_config = *config;
    s_initialized = true;
    return IMU_SUCCESS;
}

/********************************************************************
**函数名称:  imu_get_chip_id
**入口参数:  无
**出口参数:  id       ---        芯片标识值
**函数功能:  获取当前六轴芯片标识
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_get_chip_id(uint8_t *id)
{
    if ((imu_check_initialized() != IMU_SUCCESS) || (id == NULL))
    {
        return (id == NULL) ? IMU_ERROR_PARAM : IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_driver_get_id(&s_driver, id));
}

/********************************************************************
**函数名称:  imu_set_config
**入口参数:  config   ---        六轴工作配置（输入）
**出口参数:  无
**函数功能:  更新六轴采样和滤波配置
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_config(const imu_config_t *config)
{
    qmi8658b_config_t driver_config;
    int ret;

    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    ret = imu_make_driver_config(config, &driver_config);
    if (ret == 0)
    {
        ret = qmi8658b_driver_set_config(&s_driver, &driver_config);
    }

    if (ret == 0)
    {
        s_config = *config;
    }

    return imu_convert_result(ret);
}

/********************************************************************
**函数名称:  imu_set_power_mode
**入口参数:  mode     ---        目标电源模式（输入）
**出口参数:  无
**函数功能:  切换六轴传感器电源模式（正常/低功耗/陀螺休眠/挂起/掉电）
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_power_mode(imu_power_mode_t mode)
{
    uint8_t sensor_enable;
    bool gyro_snooze;
    bool power_down;
    int ret;

    if (mode >= IMU_POWER_MAX)
    {
        return IMU_ERROR_PARAM;
    }

    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    gyro_snooze = false;
    power_down = false;

    switch (mode)
    {
        case IMU_POWER_DOWN:
            sensor_enable = 0U;
            power_down = true;
            break;

        case IMU_POWER_SUSPEND:
            sensor_enable = 0U;
            break;

        case IMU_POWER_LOW_POWER:
            if (!imu_is_acc_lp_odr(s_config.acc_odr))
            {
                return IMU_ERROR_PARAM;
            }

            sensor_enable = IMU_SENSOR_ACC;
            break;

        case IMU_POWER_GYRO_SNOOZE:
            sensor_enable = IMU_SENSOR_ACC | IMU_SENSOR_GYR;
            gyro_snooze = true;
            break;

        case IMU_POWER_NORMAL:
        default:
            if (imu_is_acc_lp_odr(s_config.acc_odr))
            {
                return IMU_ERROR_PARAM;
            }

            sensor_enable = IMU_SENSOR_ACC | IMU_SENSOR_GYR;
            break;
    }

    ret = qmi8658b_driver_set_power_mode(&s_driver, sensor_enable, gyro_snooze, power_down);
    if (ret == 0)
    {
        s_config.power_mode = mode;
    }

    return imu_convert_result(ret);
}

/********************************************************************
**函数名称:  imu_read_raw
**入口参数:  无
**出口参数:  raw      ---        原始六轴和温度数据
**函数功能:  读取传感器原始寄存器数据
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_raw(imu_raw_data_t *raw)
{
    int16_t data[7];
    int ret;

    if ((imu_check_initialized() != IMU_SUCCESS) || (raw == NULL))
    {
        return (raw == NULL) ? IMU_ERROR_PARAM : IMU_ERROR_INIT;
    }

    ret = qmi8658b_driver_read(&s_driver, data);
    if (ret == 0)
    {
        raw->temperature = data[0];
        raw->acc_x = data[1];
        raw->acc_y = data[2];
        raw->acc_z = data[3];
        raw->gyr_x = data[4];
        raw->gyr_y = data[5];
        raw->gyr_z = data[6];
    }

    return imu_convert_result(ret);
}

/********************************************************************
**函数名称:  imu_read
**入口参数:  无
**出口参数:  data     ---        换算后的六轴和温度数据
**函数功能:  读取并转换为通用物理单位
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read(imu_data_t *data)
{
    imu_raw_data_t raw;
    imu_result_t ret;

    if (data == NULL)
    {
        return IMU_ERROR_PARAM;
    }

    ret = imu_read_raw(&raw);
    if (ret != IMU_SUCCESS)
    {
        return ret;
    }

    // 将原始计数转换为通用接口规定的 mg 和 mdps 单位
    // 原始计数 ×1000 ÷ LSB_per_g → mg; ×1000 ÷ LSB_per_dps → mdps
    data->acc_x = ((int32_t)raw.acc_x * 1000) / s_driver.acc_lsb_per_g;
    data->acc_y = ((int32_t)raw.acc_y * 1000) / s_driver.acc_lsb_per_g;
    data->acc_z = ((int32_t)raw.acc_z * 1000) / s_driver.acc_lsb_per_g;
    data->gyr_x = ((int32_t)raw.gyr_x * 1000) / s_driver.gyr_lsb_per_dps;
    data->gyr_y = ((int32_t)raw.gyr_y * 1000) / s_driver.gyr_lsb_per_dps;
    data->gyr_z = ((int32_t)raw.gyr_z * 1000) / s_driver.gyr_lsb_per_dps;
    // QMI8658B 温度公式: T(°C) = TEMP_OUT / 256, 输出单位 0.01°C
    // ×100/256: 转为 0.01°C 单位
    data->temperature = ((int32_t)raw.temperature * 100) / 256;
    return IMU_SUCCESS;
}

/********************************************************************
**函数名称:  imu_read_temperature
**入口参数:  无
**出口参数:  temperature ---    温度值，单位 0.01 摄氏度
**函数功能:  读取换算后的温度数据
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_temperature(int32_t *temperature)
{
    int16_t temp_raw;
    int ret;

    if (temperature == NULL)
    {
        return IMU_ERROR_PARAM;
    }

    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    ret = qmi8658b_driver_read_temperature_raw(&s_driver, &temp_raw);
    if (ret == 0)
    {
        // T(°C) = TEMP_OUT / 256; ×100转为0.01°C单位
        *temperature = ((int32_t)temp_raw * 100) / 256;
    }

    return imu_convert_result(ret);
}

/********************************************************************
**函数名称:  imu_read_timestamp
**入口参数:  无
**出口参数:  timestamp ---      传感器时间戳
**函数功能:  读取传感器内部时间戳
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_timestamp(uint32_t *timestamp)
{
    if ((imu_check_initialized() != IMU_SUCCESS) || (timestamp == NULL))
    {
        return (timestamp == NULL) ? IMU_ERROR_PARAM : IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_driver_read_timestamp(&s_driver, timestamp));
}

/********************************************************************
**函数名称:  imu_read_status
**入口参数:  无
**出口参数:  status   ---        传感器状态寄存器值
**函数功能:  读取普通状态寄存器
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_status(uint8_t *status)
{
    if ((imu_check_initialized() != IMU_SUCCESS) || (status == NULL))
    {
        return (status == NULL) ? IMU_ERROR_PARAM : IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_driver_read_reg(&s_driver, QMI8658B_REG_STATUS0, status, 1U));
}

/********************************************************************
**函数名称:  imu_read_int_status
**入口参数:  无
**出口参数:  status   ---        中断状态寄存器值
**函数功能:  读取中断状态寄存器
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_int_status(uint8_t *status)
{
    if ((imu_check_initialized() != IMU_SUCCESS) || (status == NULL))
    {
        return (status == NULL) ? IMU_ERROR_PARAM : IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_driver_read_reg(&s_driver, QMI8658B_REG_STATUS_INT, status, 1U));
}

/********************************************************************
**函数名称:  imu_read_reg
**入口参数:  reg_addr ---        寄存器地址（输入）
**           len      ---        读取长度（输入）
**出口参数:  data     ---        寄存器数据
**函数功能:  读取连续寄存器
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_reg(uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_driver_read_reg(&s_driver, reg_addr, data, len));
}

/********************************************************************
**函数名称:  imu_write_reg
**入口参数:  reg_addr ---        寄存器地址（输入）
**           data     ---        写入数据（输入）
**           len      ---        写入长度（输入）
**出口参数:  无
**函数功能:  写入连续寄存器
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_write_reg(uint8_t reg_addr, const uint8_t *data, uint16_t len)
{
    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_driver_write_reg(&s_driver, reg_addr, data, len));
}

/********************************************************************
**函数名称:  imu_int_pin_enable
**入口参数:  pin      ---        中断引脚（输入）
**           enable   ---        true 使能，false 禁用（输入）
**出口参数:  无
**函数功能:  使能或禁用指定中断引脚的输出驱动
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_int_pin_enable(imu_int_pin_t pin, bool enable)
{
    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    if ((pin == IMU_INT_NONE) || (pin >= IMU_INT_PIN_MAX))
    {
        return IMU_ERROR_PARAM;
    }

    return imu_convert_result(qmi8658b_driver_enable_int_pin(&s_driver, (uint8_t)pin, enable));
}

/********************************************************************
**函数名称:  imu_int_map
**入口参数:  src      ---        中断源（输入）
**           pin      ---        目标中断引脚（输入）
**出口参数:  无
**函数功能:  映射 FIFO 或活动检测中断到指定引脚
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_int_map(imu_int_src_t src, imu_int_pin_t pin)
{
    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    if ((src >= IMU_INT_SRC_MAX) || (pin == IMU_INT_NONE) || (pin >= IMU_INT_PIN_MAX))
    {
        return IMU_ERROR_PARAM;
    }

    return imu_convert_result(qmi8658b_driver_map_interrupt(&s_driver, (uint8_t)src, (uint8_t)pin));
}

/********************************************************************
**函数名称:  imu_register_int_callback
**入口参数:  callback ---        GPIO 中断回调（输入）
**出口参数:  无
**函数功能:  注册板级 INT1 通知回调
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_register_int_callback(imu_int_callback_t callback)
{
    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_port_register_int_callback(callback));
}

/********************************************************************
**函数名称:  imu_fifo_config
**入口参数:  config   ---        FIFO 工作配置（输入）
**出口参数:  无
**函数功能:  配置 FIFO 数据源、大小、模式和水印中断
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_fifo_config(const imu_fifo_config_t *config)
{
    uint8_t sensor_enable;

    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    if ((config == NULL) || (config->mode > IMU_FIFO_STREAM) || (config->int_pin == IMU_INT_NONE) ||
        (config->int_pin >= IMU_INT_PIN_MAX) || (config->fifo_size > IMU_FIFO_SIZE_128))
    {
        return IMU_ERROR_PARAM;
    }

    sensor_enable = (config->acc_enable ? IMU_SENSOR_ACC : 0U) | (config->gyr_enable ? IMU_SENSOR_GYR : 0U);
    if (sensor_enable == 0U)
    {
        return IMU_ERROR_PARAM;
    }

    if ((sensor_enable == (IMU_SENSOR_ACC | IMU_SENSOR_GYR)) && (s_config.acc_odr != s_config.gyr_odr))
    {
        return IMU_ERROR_PARAM;
    }

    return imu_convert_result(qmi8658b_driver_config_fifo(&s_driver, sensor_enable, (uint8_t)config->mode,
                                                            (qmi8658b_fifo_size_t)config->fifo_size,
                                                            config->watermark, (uint8_t)config->int_pin));
}

/********************************************************************
**函数名称:  imu_fifo_read
**入口参数:  max_frames ---     输出帧容量（输入）
**出口参数:  frames   ---        FIFO 原始帧数据
**           frame_count ---    实际输出帧数
**函数功能:  读取并解析 FIFO 六轴原始帧
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_fifo_read(imu_raw_data_t *frames, uint16_t max_frames, uint16_t *frame_count)
{
    uint8_t frame_size;
    uint16_t data_len;
    uint16_t index;
    uint16_t offset;
    int ret;

    if ((imu_check_initialized() != IMU_SUCCESS) || (frames == NULL) || (frame_count == NULL) ||
        (max_frames == 0U) || (max_frames > IMU_FIFO_MAX_FRAMES))
    {
        return ((frames == NULL) || (frame_count == NULL) || (max_frames == 0U) || (max_frames > IMU_FIFO_MAX_FRAMES)) ?
               IMU_ERROR_PARAM : IMU_ERROR_INIT;
    }

    // 根据 FIFO 数据源确定单帧字节数：单传感器6字节，双传感器12字节
    frame_size = (s_driver.fifo_sensor_enable == (IMU_SENSOR_ACC | IMU_SENSOR_GYR)) ?
                 IMU_FIFO_FRAME_BYTES_6DOF : IMU_FIFO_FRAME_BYTES_3DOF;
    ret = qmi8658b_driver_read_fifo(&s_driver, s_fifo_buffer, (uint16_t)(max_frames * frame_size), &data_len);
    if (ret != 0)
    {
        return imu_convert_result(ret);
    }

    *frame_count = (uint16_t)(data_len / frame_size);
    for (index = 0U; index < *frame_count; index++)
    {
        memset(&frames[index], 0, sizeof(frames[index]));
        offset = (uint16_t)(index * frame_size);

        if ((s_driver.fifo_sensor_enable & IMU_SENSOR_ACC) != 0U)
        {
            frames[index].acc_x = (int16_t)((uint16_t)s_fifo_buffer[offset] |
                                   ((uint16_t)s_fifo_buffer[offset + 1U] << 8));
            frames[index].acc_y = (int16_t)((uint16_t)s_fifo_buffer[offset + 2U] |
                                   ((uint16_t)s_fifo_buffer[offset + 3U] << 8));
            frames[index].acc_z = (int16_t)((uint16_t)s_fifo_buffer[offset + 4U] |
                                   ((uint16_t)s_fifo_buffer[offset + 5U] << 8));
            offset = (uint16_t)(offset + IMU_FIFO_FRAME_BYTES_3DOF);
        }

        if ((s_driver.fifo_sensor_enable & IMU_SENSOR_GYR) != 0U)
        {
            frames[index].gyr_x = (int16_t)((uint16_t)s_fifo_buffer[offset] |
                                   ((uint16_t)s_fifo_buffer[offset + 1U] << 8));
            frames[index].gyr_y = (int16_t)((uint16_t)s_fifo_buffer[offset + 2U] |
                                   ((uint16_t)s_fifo_buffer[offset + 3U] << 8));
            frames[index].gyr_z = (int16_t)((uint16_t)s_fifo_buffer[offset + 4U] |
                                   ((uint16_t)s_fifo_buffer[offset + 5U] << 8));
        }
    }

    return IMU_SUCCESS;
}

/********************************************************************
**函数名称:  imu_fifo_flush
**入口参数:  无
**出口参数:  无
**函数功能:  清空当前 FIFO 数据
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_fifo_flush(void)
{
    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_driver_flush_fifo(&s_driver));
}

/********************************************************************
**函数名称:  imu_fifo_get_status
**入口参数:  无
**出口参数:  status   ---        FIFO 状态寄存器值
**函数功能:  读取 FIFO_STATUS 寄存器值
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_fifo_get_status(uint8_t *status)
{
    if ((imu_check_initialized() != IMU_SUCCESS) || (status == NULL))
    {
        return (status == NULL) ? IMU_ERROR_PARAM : IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_driver_get_fifo_status(&s_driver, status));
}

/********************************************************************
**函数名称:  imu_feature_enable
**入口参数:  feature  ---        目标嵌入式特性（输入）
**           enable   ---        true 使能，false 禁用（输入）
**出口参数:  无
**函数功能:  控制运动检测和敲击特性
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_feature_enable(imu_feature_t feature, bool enable)
{
    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    if (feature >= IMU_FEATURE_MAX)
    {
        return IMU_ERROR_PARAM;
    }

    return imu_convert_result(qmi8658b_driver_feature_enable(&s_driver, (uint8_t)feature, enable));
}

/********************************************************************
**函数名称:  imu_set_motion_config
**入口参数:  config   ---        运动检测配置参数（输入）
**出口参数:  无
**函数功能:  按数据手册的两段 CTRL9 流程配置运动检测
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_motion_config(const imu_motion_config_t *config)
{
    uint8_t config_set1[8];
    uint8_t config_set2[8];

    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    if (config == NULL)
    {
        return IMU_ERROR_PARAM;
    }

    // 第一组参数按数据手册 Table 35 顺序写入 CAL1~CAL4
    config_set1[0] = config->any_motion_threshold_x;
    config_set1[1] = config->any_motion_threshold_y;
    config_set1[2] = config->any_motion_threshold_z;
    config_set1[3] = config->no_motion_threshold_x;
    config_set1[4] = config->no_motion_threshold_y;
    config_set1[5] = config->no_motion_threshold_z;
    config_set1[6] = config->mode_ctrl;  // MOTION_MODE_CTRL: 轴使能和逻辑配置
    config_set1[7] = 0x01U;              // CAL4_H=0x01，标识第一组参数
    // 第二组参数按数据手册 Table 35 顺序写入 CAL1~CAL4
    config_set2[0] = config->any_motion_window;
    config_set2[1] = config->no_motion_window;
    config_set2[2] = (uint8_t)(config->sig_motion_wait_window & 0x00FFU);        // 低字节
    config_set2[3] = (uint8_t)((config->sig_motion_wait_window >> 8) & 0x00FFU); // 高字节
    config_set2[4] = (uint8_t)(config->sig_motion_confirm_window & 0x00FFU);
    config_set2[5] = (uint8_t)((config->sig_motion_confirm_window >> 8) & 0x00FFU);
    config_set2[6] = 0U;                 // CAL4_L 第二组不用
    config_set2[7] = 0x02U;              // CAL4_H=0x02，标识第二组参数

    return imu_convert_result(qmi8658b_driver_set_motion_config(&s_driver, config_set1, config_set2));
}

/********************************************************************
**函数名称:  imu_set_tap_config
**入口参数:  config   ---        敲击检测配置参数（输入）
**出口参数:  无
**函数功能:  按数据手册的两段 CTRL9 流程配置敲击检测(目前敲击检测功能并没有生效,后续若需要使用再问原厂)
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_tap_config(const imu_tap_config_t *config)
{
    uint8_t config_set1[8];
    uint8_t config_set2[8];

    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    if (config == NULL)
    {
        return IMU_ERROR_PARAM;
    }

    // 第一组参数按数据手册 Table 37 顺序写入 CAL1~CAL4
    config_set1[0] = config->peak_window;
    config_set1[1] = config->priority;           // Priority[2:0], 定义三轴优先级
    config_set1[2] = (uint8_t)(config->tap_window & 0x00FFU);           // TapWindow 低字节
    config_set1[3] = (uint8_t)((config->tap_window >> 8) & 0x00FFU);    // TapWindow 高字节
    config_set1[4] = (uint8_t)(config->double_tap_window & 0x00FFU);    // DTapWindow 低字节
    config_set1[5] = (uint8_t)((config->double_tap_window >> 8) & 0x00FFU); // DTapWindow 高字节
    config_set1[6] = 0U;                 // CAL4_L 第一组不用
    config_set1[7] = 0x01U;              // CAL4_H=0x01，标识第一组参数
    // 第二组参数按数据手册 Table 37 顺序写入 CAL1~CAL4
    config_set2[0] = config->alpha;
    config_set2[1] = config->gamma;
    config_set2[2] = (uint8_t)(config->peak_magnitude_threshold & 0x00FFU);      // PeakMagThr 低字节
    config_set2[3] = (uint8_t)((config->peak_magnitude_threshold >> 8) & 0x00FFU); // PeakMagThr 高字节
    config_set2[4] = (uint8_t)(config->undefined_motion_threshold & 0x00FFU);     // UDMThr 低字节
    config_set2[5] = (uint8_t)((config->undefined_motion_threshold >> 8) & 0x00FFU); // UDMThr 高字节
    config_set2[6] = 0U;                 // CAL4_L 第二组不用
    config_set2[7] = 0x02U;              // CAL4_H=0x02，标识第二组参数

    return imu_convert_result(qmi8658b_driver_set_tap_config(&s_driver, config_set1, config_set2));
}

/********************************************************************
**函数名称:  imu_get_tap_status
**入口参数:  无
**出口参数:  status   ---        敲击次数、轴和方向
**函数功能:  读取并解析 TAP_STATUS 寄存器
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_get_tap_status(imu_tap_status_t *status)
{
    uint8_t raw_status;
    int ret;

    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    if (status == NULL)
    {
        return IMU_ERROR_PARAM;
    }

    ret = qmi8658b_driver_get_tap_status(&s_driver, &raw_status);
    if (ret == 0)
    {
        status->tap_number = raw_status & 0x03U;                 // bits[1:0]=TAP_NUM: 0=无,1=单击,2=双击
        status->axis = (raw_status >> 4) & 0x03U;                // bits[5:4]=TAP_AXIS: 1=X,2=Y,3=Z
        status->negative_polarity = (raw_status & 0x80U) != 0U;  // bit7=TAP_POLARITY: 1=负方向
    }

    return imu_convert_result(ret);
}

/********************************************************************
**函数名称:  imu_get_motion_status
**入口参数:  无
**出口参数:  status   ---        运动检测实时状态
**函数功能:  读取 STATUS1 寄存器解析运动检测各事件标志
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_get_motion_status(imu_motion_status_t *status)
{
    qmi8658b_motion_status_t driver_status;
    int ret;

    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    if (status == NULL)
    {
        return IMU_ERROR_PARAM;
    }

    ret = qmi8658b_driver_get_motion_status(&s_driver, &driver_status);
    if (ret == 0)
    {
        status->any_motion = driver_status.any_motion;
        status->no_motion = driver_status.no_motion;
        status->sig_motion = driver_status.sig_motion;
        status->tap = driver_status.tap;
    }

    return imu_convert_result(ret);
}

/********************************************************************
**函数名称:  imu_set_sync_sample
**入口参数:  enable   ---        true 使能同步采样，false 禁用
**出口参数:  无
**函数功能:  配置同步采样锁定读取模式
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_sync_sample(bool enable)
{
    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_driver_set_sync_sample(&s_driver, enable));
}

/********************************************************************
**函数名称:  imu_get_chip_info
**入口参数:  无
**出口参数:  info     ---        固件版本和芯片唯一标识
**函数功能:  读取 QMI8658B 固件版本和 USID
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_get_chip_info(imu_chip_info_t *info)
{
    qmi8658b_chip_info_t driver_info;
    int ret;

    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    if (info == NULL)
    {
        return IMU_ERROR_PARAM;
    }

    ret = qmi8658b_driver_get_chip_info(&s_driver, &driver_info);
    if (ret == 0)
    {
        memcpy(info, &driver_info, sizeof(*info));
    }

    return imu_convert_result(ret);
}

/********************************************************************
**函数名称:  imu_set_acc_offset
**入口参数:  offset   ---        三轴加速度计偏置，格式 signed 4.12
**出口参数:  无
**函数功能:  设置加速度计主机偏置
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_acc_offset(const int16_t offset[3])
{
    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    if (offset == NULL)
    {
        return IMU_ERROR_PARAM;
    }

    return imu_convert_result(qmi8658b_driver_set_acc_offset(&s_driver, offset));
}

/********************************************************************
**函数名称:  imu_set_gyr_offset
**入口参数:  offset   ---        三轴陀螺仪偏置，格式 signed 11.5
**出口参数:  无
**函数功能:  设置陀螺仪主机偏置
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_gyr_offset(const int16_t offset[3])
{
    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    if (offset == NULL)
    {
        return IMU_ERROR_PARAM;
    }

    return imu_convert_result(qmi8658b_driver_set_gyr_offset(&s_driver, offset));
}

/********************************************************************
**函数名称:  imu_run_calibration
**入口参数:  无
**出口参数:  gain     ---        陀螺仪校准增益数据
**函数功能:  执行芯片片内校准
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_run_calibration(uint8_t gain[6])
{
    if ((imu_check_initialized() != IMU_SUCCESS) || (gain == NULL))
    {
        return (gain == NULL) ? IMU_ERROR_PARAM : IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_driver_run_calibration(&s_driver, gain));
}

/********************************************************************
**函数名称:  imu_apply_gyro_gain
**入口参数:  gain     ---        陀螺仪校准增益数据（输入）
**出口参数:  无
**函数功能:  应用已保存的陀螺仪校准数据
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_apply_gyro_gain(const uint8_t gain[6])
{
    if ((imu_check_initialized() != IMU_SUCCESS) || (gain == NULL))
    {
        return (gain == NULL) ? IMU_ERROR_PARAM : IMU_ERROR_INIT;
    }

    return imu_convert_result(qmi8658b_driver_apply_gyro_gain(&s_driver, gain));
}

/********************************************************************
**函数名称:  imu_run_self_test
**入口参数:  sensor_mask ---    自检传感器使能位（输入）
**出口参数:  result       ---        自检输出值与判定结果
**函数功能:  触发六轴传感器硬件自检并读取判定结果
**返回值:    IMU_SUCCESS 表示自检通过，IMU_ERROR_SELF_TEST 表示未通过
*********************************************************************/
imu_result_t imu_run_self_test(uint8_t sensor_mask, imu_self_test_result_t *result)
{
    qmi8658b_self_test_result_t drv_result;
    int ret;

    if (imu_check_initialized() != IMU_SUCCESS)
    {
        return IMU_ERROR_INIT;
    }

    ret = qmi8658b_driver_run_self_test(&s_driver, sensor_mask, &drv_result);
    if (result != NULL)
    {
        result->acc_pass = drv_result.acc_pass;
        result->gyr_pass = drv_result.gyr_pass;
        result->acc_x_mg = drv_result.acc_x_mg;
        result->acc_y_mg = drv_result.acc_y_mg;
        result->acc_z_mg = drv_result.acc_z_mg;
        result->gyr_x_mdps = drv_result.gyr_x_mdps;
        result->gyr_y_mdps = drv_result.gyr_y_mdps;
        result->gyr_z_mdps = drv_result.gyr_z_mdps;
    }

    if (ret == 0)
    {
        if (((sensor_mask & IMU_SENSOR_ACC) != 0U) && !drv_result.acc_pass)
        {
            return IMU_ERROR_SELF_TEST;
        }

        if (((sensor_mask & IMU_SENSOR_GYR) != 0U) && !drv_result.gyr_pass)
        {
            return IMU_ERROR_SELF_TEST;
        }
    }

    return imu_convert_result(ret);
}
