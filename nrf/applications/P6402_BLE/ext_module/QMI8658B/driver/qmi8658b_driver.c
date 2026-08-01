/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        qmi8658b_driver.c
**文件描述:        QMI8658B 芯片寄存器驱动实现文件
**当前版本:        V1.0
**作    者:        周森达 (zhousenda@jimiiot.com)
**完成日期:        2026.07.30
*********************************************************************
** 功能描述:        实现 QMI8658B 寄存器配置、数据采集、FIFO、中断、特性和校准控制
*********************************************************************/

#include "qmi8658b_driver.h"
#include "qmi8658b_reg.h"

#include <errno.h>
#include <string.h>

#define QMI8658B_FIFO_MAX_WORDS           1023U   /* 芯片 FIFO 最大计数，单位 16 位字 */
#define QMI8658B_INT_SRC_FIFO_WATERMARK   0U      /* FIFO 水位中断源索引 */
#define QMI8658B_INT_SRC_ACTIVITY         1U      /* Any/No/Sig/Tap 共用活动检测中断索引 */
#define QMI8658B_FEATURE_ANY_MOTION       0U      /* 任意运动特性索引 */
#define QMI8658B_FEATURE_NO_MOTION        1U      /* 静止检测特性索引 */
#define QMI8658B_FEATURE_SIG_MOTION       2U      /* 显著运动特性索引 */
#define QMI8658B_FEATURE_TAP              3U      /* 敲击检测特性索引 */

/********************************************************************
**函数名称:  qmi8658b_driver_compute_ctrl5
**入口参数:  lpf_mode ---        低通滤波模式 0~3（输入）
**出口参数:  无
**函数功能:  根据模式索引计算 CTRL5 寄存器值
**返回值:    CTRL5 寄存器值（使能态）
**注意事项:  模式 0=2.66%ODR(最强滤波)，模式 3=13.37%ODR(最弱滤波)
*********************************************************************/
static uint8_t qmi8658b_driver_compute_ctrl5(uint8_t lpf_mode)
{
    if (lpf_mode > 3U)
    {
        lpf_mode = 0U;
    }

    // CTRL5 = gLPF_MODE[6:5] | gLPF_EN[4] | aLPF_MODE[2:1] | aLPF_EN[0]
    // lpf_mode << 5 → gLPF_MODE bits[6:5], lpf_mode << 1 → aLPF_MODE bits[2:1]
    return (uint8_t)(((uint8_t)(lpf_mode << 5) & 0x60U) | QMI8658B_CTRL5_GYR_LPF_EN |
                     ((uint8_t)(lpf_mode << 1) & 0x06U) | QMI8658B_CTRL5_ACC_LPF_EN);
}

/********************************************************************
**函数名称:  qmi8658b_driver_check
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  无
**函数功能:  校验驱动上下文与总线回调
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int qmi8658b_driver_check(const qmi8658b_driver_t *driver)
{
    if ((driver == NULL) || (driver->bus.read == NULL) || (driver->bus.write == NULL) ||
        (driver->bus.delay_ms == NULL))
    {
        return -EINVAL;
    }

    return 0;
}

/********************************************************************
**函数名称:  qmi8658b_driver_set_scale
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  无
**函数功能:  根据量程更新原始数据灵敏度
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int qmi8658b_driver_set_scale(qmi8658b_driver_t *driver)
{
    uint8_t acc_index;
    uint8_t gyr_index;

    acc_index = (uint8_t)(driver->config.acc_range >> 4);     // acc_range 在 bits[6:4], 右移4位得量程索引 0~3
    gyr_index = (uint8_t)(driver->config.gyr_range >> 4);     // gyr_range 在 bits[6:4], 右移4位得量程索引 0~7
    if ((acc_index > 3U) || (gyr_index > 7U))
    {
        return -EINVAL;
    }

    driver->acc_lsb_per_g = (uint16_t)(16384U >> acc_index);      // ±2g:16384, ±4g:8192, ±8g:4096, ±16g:2048
    driver->gyr_lsb_per_dps = (uint16_t)(2048U >> gyr_index);     // ±16dps:2048 … ±2048dps:16

    return 0;
}

/********************************************************************
**函数名称:  qmi8658b_driver_write_u8
**入口参数:  driver   ---        驱动上下文（输入）
**           reg_addr ---        寄存器地址（输入）
**           value    ---        写入数据（输入）
**出口参数:  无
**函数功能:  写入单字节寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int qmi8658b_driver_write_u8(qmi8658b_driver_t *driver, uint8_t reg_addr, uint8_t value)
{
    return driver->bus.write(driver->bus.context, reg_addr, &value, 1U);
}

/********************************************************************
**函数名称:  qmi8658b_driver_read_u8
**入口参数:  driver   ---        驱动上下文（输入）
**           reg_addr ---        寄存器地址（输入）
**出口参数:  value    ---        读取数据（输出）
**函数功能:  读取单字节寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int qmi8658b_driver_read_u8(qmi8658b_driver_t *driver, uint8_t reg_addr, uint8_t *value)
{
    return driver->bus.read(driver->bus.context, reg_addr, value, 1U);
}

/********************************************************************
**函数名称:  qmi8658b_driver_config_sensors
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  无
**函数功能:  配置加速度计、陀螺仪和低通滤波器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int qmi8658b_driver_config_sensors(qmi8658b_driver_t *driver)
{
    uint8_t ctrl5;
    int ret;

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL2,
                                    (uint8_t)(driver->config.acc_range | driver->config.acc_odr));
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL3,
                                    (uint8_t)(driver->config.gyr_range | driver->config.gyr_odr));
    if (ret != 0)
    {
        return ret;
    }

    // 根据配置的 lpf_mode 计算 CTRL5 值，模式 0 为默认最强滤波
    ctrl5 = driver->config.lpf_enable ? qmi8658b_driver_compute_ctrl5(driver->config.lpf_mode) : 0x00U;
    return qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL5, ctrl5);
}

/********************************************************************
**函数名称:  qmi8658b_driver_init
**入口参数:  driver   ---        驱动上下文（输入）
**           bus      ---        总线回调集合（输入）
**           config   ---        传感器配置（输入）
**出口参数:  无
**函数功能:  复位并初始化 QMI8658B
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_init(qmi8658b_driver_t *driver, const qmi8658b_bus_t *bus, const qmi8658b_config_t *config)
{
    uint8_t id;
    uint32_t count;
    int ret;

    if ((driver == NULL) || (bus == NULL) || (config == NULL))
    {
        return -EINVAL;
    }

    memset(driver, 0, sizeof(*driver));
    driver->bus = *bus;
    driver->config = *config;
    ret = qmi8658b_driver_check(driver);
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_set_scale(driver);
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_WHO_AM_I, &id);
    if (ret != 0)
    {
        return ret;
    }

    if (id != QMI8658B_CHIP_ID)
    {
        return -ENODEV;
    }

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_RESET, 0xB0U);    // 数据手册规定的软复位触发值
    if (ret != 0)
    {
        return ret;
    }

    // 数据手册要求：软复位后立即读取 0x4D，成功复位时该寄存器返回 0x80
    for (count = 0U; count < QMI8658B_COMMAND_TIMEOUT_MS; count++)
    {
        ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_RESET_DONE, &id);
        if ((ret == 0) && (id == 0x80U))
        {
            break;
        }

        driver->bus.delay_ms(1U);
    }

    if (count >= QMI8658B_COMMAND_TIMEOUT_MS)
    {
        return -ETIMEDOUT;
    }

    ret = qmi8658b_driver_config_sensors(driver);
    if (ret != 0)
    {
        return ret;
    }

    // 设置 ADDR_AI=1(地址自增), BE=0(小端模式, 与后续数据解析一致), INT1 使能
    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL1,
                                    QMI8658B_CTRL1_ADDR_AI | QMI8658B_CTRL1_INT1_ENABLE);  // 0x40|0x08 = 0x48
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL8, driver->config.ctrl8_value);
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, driver->config.sensor_enable);
    if (ret == 0)
    {
        driver->initialized = true;
        driver->bus.delay_ms(2U);       // 传感器使能后等待振荡器和数据通路稳定
    }

    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_get_id
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  id       ---        芯片标识值
**函数功能:  读取芯片标识寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_get_id(qmi8658b_driver_t *driver, uint8_t *id)
{
    if (id == NULL)
    {
        return -EINVAL;
    }

    return qmi8658b_driver_read_u8(driver, QMI8658B_REG_WHO_AM_I, id);
}

/********************************************************************
**函数名称:  qmi8658b_driver_set_config
**入口参数:  driver   ---        驱动上下文（输入）
**           config   ---        芯片工作配置（输入）
**出口参数:  无
**函数功能:  更新芯片采样和电源配置
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_config(qmi8658b_driver_t *driver, const qmi8658b_config_t *config)
{
    int ret;

    if ((driver == NULL) || (config == NULL))
    {
        return -EINVAL;
    }

    if (driver->sync_sample)
    {
        return -EBUSY;
    }

    driver->config = *config;
    ret = qmi8658b_driver_set_scale(driver);
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_config_sensors(driver);
    if (ret != 0)
    {
        return ret;
    }

    return qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7,
                                    (uint8_t)(driver->config.sensor_enable |
                                    (driver->sync_sample ? QMI8658B_CTRL7_SYNC_SAMPLE : 0U)));
}

/********************************************************************
**函数名称:  qmi8658b_driver_read
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  data     ---        温度、加速度和陀螺仪原始数据
**函数功能:  等待数据就绪后连续读取温度和六轴寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_read(qmi8658b_driver_t *driver, int16_t data[7])
{
    uint8_t buffer[14];
    uint8_t status;
    uint8_t acc_gyr_ready;
    uint32_t timeout;
    uint8_t index;
    int ret;

    if ((driver == NULL) || (data == NULL) || !driver->initialized)
    {
        return -EINVAL;
    }

    if (driver->sync_sample)
    {
        // SyncSample 模式: 等待 STATUSINT.bit0(Avail)=1 且 bit1(Locked)=1
        for (timeout = 0U; timeout < QMI8658B_DRDY_TIMEOUT_MS; timeout++)
        {
            ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_STATUS_INT, &status);
            if ((ret == 0) && ((status & 0x03U) == 0x03U))    // bit[1:0]=Avail|Locked, 均置位表示数据已锁定可读
            {
                break;
            }

            driver->bus.delay_ms(1U);
        }

        if (timeout >= QMI8658B_DRDY_TIMEOUT_MS)
        {
            return -ETIMEDOUT;
        }
    }
    else
    {
        // 非 SyncSample 模式: 检查 STATUS0 的 aDA 和 gDA 数据就绪位
        acc_gyr_ready = (driver->config.sensor_enable & QMI8658B_CTRL7_ACC_ENABLE) ? QMI8658B_STATUS0_ACC_DRDY : 0U;
        acc_gyr_ready |= (driver->config.sensor_enable & QMI8658B_CTRL7_GYR_ENABLE) ? QMI8658B_STATUS0_GYR_DRDY : 0U;

        for (timeout = 0U; timeout < QMI8658B_DRDY_TIMEOUT_MS; timeout++)
        {
            ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_STATUS0, &status);
            if ((ret == 0) && ((status & acc_gyr_ready) == acc_gyr_ready))
            {
                break;
            }

            driver->bus.delay_ms(1U);
        }

        if (timeout >= QMI8658B_DRDY_TIMEOUT_MS)
        {
            return -ETIMEDOUT;
        }
    }

    ret = driver->bus.read(driver->bus.context, QMI8658B_REG_TEMP_L, buffer, sizeof(buffer));
    if (ret != 0)
    {
        return ret;
    }

    for (index = 0U; index < 7U; index++)
    {
        // 芯片输出为低字节在前的 16 位补码数据
        data[index] = (int16_t)((uint16_t)buffer[index * 2U] | ((uint16_t)buffer[index * 2U + 1U] << 8));
    }

    return 0;
}

/********************************************************************
**函数名称:  qmi8658b_driver_read_temperature_raw
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  temp_raw ---        温度补码原始值
**函数功能:  读取温度寄存器原始值
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_read_temperature_raw(qmi8658b_driver_t *driver, int16_t *temp_raw)
{
    uint8_t buffer[2];
    int ret;

    if ((driver == NULL) || (temp_raw == NULL) || !driver->initialized)
    {
        return -EINVAL;
    }

    ret = driver->bus.read(driver->bus.context, QMI8658B_REG_TEMP_L, buffer, sizeof(buffer));
    if (ret == 0)
    {
        *temp_raw = (int16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8));
    }

    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_read_timestamp
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  timestamp ---      24 位传感器时间戳
**函数功能:  读取芯片时间戳寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_read_timestamp(qmi8658b_driver_t *driver, uint32_t *timestamp)
{
    uint8_t data[3];
    int ret;

    if ((timestamp == NULL) || (driver == NULL))
    {
        return -EINVAL;
    }

    ret = driver->bus.read(driver->bus.context, QMI8658B_REG_TIMESTAMP_L, data, sizeof(data));
    if (ret == 0)
    {
        *timestamp = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16); // 24位时间戳拼接
    }

    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_read_reg
**入口参数:  driver   ---        驱动上下文（输入）
**           reg_addr ---        寄存器地址（输入）
**           len      ---        读取长度（输入）
**出口参数:  data     ---        寄存器数据
**函数功能:  读取任意连续寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_read_reg(qmi8658b_driver_t *driver, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    if ((driver == NULL) || (data == NULL) || (len == 0U))
    {
        return -EINVAL;
    }

    return driver->bus.read(driver->bus.context, reg_addr, data, len);
}

/********************************************************************
**函数名称:  qmi8658b_driver_write_reg
**入口参数:  driver   ---        驱动上下文（输入）
**           reg_addr ---        寄存器地址（输入）
**           data     ---        写入数据（输入）
**           len      ---        写入长度（输入）
**出口参数:  无
**函数功能:  写入任意连续寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_write_reg(qmi8658b_driver_t *driver, uint8_t reg_addr, const uint8_t *data, uint16_t len)
{
    if ((driver == NULL) || (data == NULL) || (len == 0U))
    {
        return -EINVAL;
    }

    return driver->bus.write(driver->bus.context, reg_addr, data, len);
}

/********************************************************************
**函数名称:  qmi8658b_driver_send_command
**入口参数:  driver   ---        驱动上下文（输入）
**           command  ---        CTRL9 命令值（输入）
**出口参数:  无
**函数功能:  发送 CTRL9 命令并等待芯片完成
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_send_command(qmi8658b_driver_t *driver, uint8_t command)
{
    uint8_t status;
    uint32_t count;
    int ret;

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL9, command);
    if (ret != 0)
    {
        return ret;
    }

    // CTRL9 协议阶段1: 等待 STATUSINT.bit7 置位（芯片命令执行完成）
    for (count = 0U; count < QMI8658B_COMMAND_TIMEOUT_MS; count++)
    {
        ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_STATUS_INT, &status);
        if (ret != 0)
        {
            return ret;
        }

        if ((status & QMI8658B_STATUS_INT_CMD_DONE) != 0U)
        {
            break;
        }

        driver->bus.delay_ms(1U);
    }

    if (count >= QMI8658B_COMMAND_TIMEOUT_MS)
    {
        return -ETIMEDOUT;
    }

    // CTRL9 协议阶段2: 写 ACK 并等待 STATUSINT.bit7 清零（芯片确认握手完成）
    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL9, QMI8658B_CMD_ACK);
    if (ret != 0)
    {
        return ret;
    }

    for (count = 0U; count < QMI8658B_COMMAND_TIMEOUT_MS; count++)
    {
        ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_STATUS_INT, &status);
        if (ret != 0)
        {
            return ret;
        }

        if ((status & QMI8658B_STATUS_INT_CMD_DONE) == 0U)
        {
            break;
        }

        driver->bus.delay_ms(1U);
    }

    if (count >= QMI8658B_COMMAND_TIMEOUT_MS)
    {
        return -ETIMEDOUT;
    }

    return 0;
}

/********************************************************************
/********************************************************************
**函数名称:  qmi8658b_driver_config_engine
**入口参数:  driver      ---      驱动上下文（输入）
**           config_set1 ---      第一组 CAL 参数（输入）
**           config_set2 ---      第二组 CAL 参数（输入）
**           command     ---      CTRL9 配置命令（输入）
**出口参数:  无
**函数功能:  在传感器关闭状态下分两次写入嵌入式算法参数
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int qmi8658b_driver_config_engine(qmi8658b_driver_t *driver, const uint8_t config_set1[8],
                                          const uint8_t config_set2[8], uint8_t command)
{
    uint8_t ctrl7;
    int restore_ret;
    int ret;

    if ((driver == NULL) || (config_set1 == NULL) || (config_set2 == NULL))
    {
        return -EINVAL;
    }

    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL7, &ctrl7);
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, 0U);
    if (ret != 0)
    {
        return ret;
    }

    ret = driver->bus.write(driver->bus.context, QMI8658B_REG_CAL1_L, config_set1, 8U);
    if (ret != 0)
    {
        goto restore;
    }

    ret = qmi8658b_driver_send_command(driver, command);
    if (ret != 0)
    {
        goto restore;
    }

    ret = driver->bus.write(driver->bus.context, QMI8658B_REG_CAL1_L, config_set2, 8U);
    if (ret != 0)
    {
        goto restore;
    }

    ret = qmi8658b_driver_send_command(driver, command);

restore:
    restore_ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, ctrl7);
    if ((ret == 0) && (restore_ret != 0))
    {
        ret = restore_ret;
    }

    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_enable_int_pin
**入口参数:  driver   ---        驱动上下文（输入）
**           pin      ---        中断引脚编号，仅支持 1（输入）
**           enable   ---        true 使能，false 禁用（输入）
**出口参数:  无
**函数功能:  使能或禁用 INT1 引脚的输出驱动
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_enable_int_pin(qmi8658b_driver_t *driver, uint8_t pin, bool enable)
{
    uint8_t ctrl1;
    int ret;

    if (pin != 1U)
    {
        return -ENOTSUP;
    }

    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL1, &ctrl1);
    if (ret != 0)
    {
        return ret;
    }

    // 保持 ADDR_AI=1(地址自增)、BE=0(小端)和 SIM=0，与原始数据解析顺序一致
    ctrl1 |= QMI8658B_CTRL1_ADDR_AI;
    ctrl1 &= ~0xA0U;    // 清除 SIM(bit7) 和 BE(bit5)，确保小端模式

    ctrl1 = enable ? (uint8_t)(ctrl1 | QMI8658B_CTRL1_INT1_ENABLE) :
                     (uint8_t)(ctrl1 & ~QMI8658B_CTRL1_INT1_ENABLE);

    return qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL1, ctrl1);
}

/********************************************************************
**函数名称:  qmi8658b_driver_map_interrupt
**入口参数:  driver   ---        驱动上下文（输入）
**           source   ---        中断源索引（输入）
**           pin      ---        目标引脚编号（输入）
**出口参数:  无
**函数功能:  映射 FIFO 或全部活动检测事件到 INT1 引脚
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_map_interrupt(qmi8658b_driver_t *driver, uint8_t source, uint8_t pin)
{
    uint8_t ctrl1;
    uint8_t ctrl8;
    int ret;

    if (pin != 1U)
    {
        return -ENOTSUP;
    }

    if (source > QMI8658B_INT_SRC_ACTIVITY)
    {
        return -EINVAL;
    }

    if (source == QMI8658B_INT_SRC_FIFO_WATERMARK)
    {
        ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL1, &ctrl1);
        if (ret != 0)
        {
            return ret;
        }

        ctrl1 = (uint8_t)(ctrl1 | QMI8658B_CTRL1_FIFO_INT1);
        return qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL1, ctrl1);
    }

    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL8, &ctrl8);
    if (ret != 0)
    {
        return ret;
    }

    ctrl8 = (uint8_t)(ctrl8 | QMI8658B_CTRL8_INT_SEL);
    return qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL8, ctrl8);
}

/********************************************************************
**函数名称:  qmi8658b_driver_config_fifo
**入口参数:  driver        ---     驱动上下文（输入）
**           sensor_enable ---     FIFO 数据源使能位（输入）
**           mode          ---     FIFO 工作模式（输入）
**           fifo_size     ---     FIFO 大小（输入）
**           watermark     ---     FIFO 水印阈值（输入）
**           pin           ---     水印中断引脚（输入）
**出口参数:  无
**函数功能:  配置 FIFO 缓冲和水印中断
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_config_fifo(qmi8658b_driver_t *driver, uint8_t sensor_enable, uint8_t mode,
                                 qmi8658b_fifo_size_t fifo_size, uint8_t watermark, uint8_t pin)
{
    uint8_t fifo_ctrl;
    uint8_t ctrl7;
    int ret;

    if ((mode > 2U) || ((sensor_enable & ~0x03U) != 0U) || (fifo_size > QMI8658B_FIFO_SIZE_128)) // mode: 0=Bypass,1=FIFO,2=Stream
    {
        return -EINVAL;
    }

    if (driver->sync_sample)
    {
        return -ENOTSUP;
    }

    // 验证水印不超过 FIFO 大小（16样本×2^size: 16/32/64/128）
    if (watermark > (uint8_t)(16U << (uint8_t)fifo_size))
    {
        return -EINVAL;
    }

    ret = qmi8658b_driver_map_interrupt(driver, QMI8658B_INT_SRC_FIFO_WATERMARK, pin);
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_FIFO_CTRL, 0U);
    if (ret != 0)
    {
        return ret;
    }

    // 配置前清除残留帧，避免新旧配置数据混用
    ret = qmi8658b_driver_send_command(driver, QMI8658B_CMD_RESET_FIFO);
    if (ret != 0)
    {
        return ret;
    }

    // FIFO_CTRL: bit7=0(写模式), bits[3:2]=FIFO_SIZE, bits[1:0]=FIFO_MODE
    fifo_ctrl = (uint8_t)(((uint8_t)fifo_size << 2) | mode);
    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_FIFO_WATERMARK, watermark);
    if (ret != 0)
    {
        return ret;
    }

    // 保存 FIFO 数据源配置到独立字段，避免覆盖 config.sensor_enable
    driver->fifo_sensor_enable = sensor_enable;

    // FIFO 非旁路模式需关闭 DRDY，避免数据就绪和 FIFO 水印同时输出
    if (mode > 0U)
    {
        sensor_enable = (uint8_t)(sensor_enable | QMI8658B_CTRL7_DRDY_DISABLE);
    }

    // 先读取当前 CTRL7，保留除使能位和 DRDY_DIS 外的其他配置位
    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL7, &ctrl7);
    if (ret != 0)
    {
        return ret;
    }

    // 仅更新使能位和 DRDY_DIS，保留 SyncSample/gSN/保留位
    // 0xDC=1101_1100 保留 bits[7,6,4,3,2]; 0x23=0010_0011 提取 bits[5,1,0]
    ctrl7 = (uint8_t)((ctrl7 & 0xDCU) | (sensor_enable & 0x23U));

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_FIFO_CTRL, fifo_ctrl);
    if (ret != 0)
    {
        return ret;
    }

    return qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, ctrl7);
}

/********************************************************************
**函数名称:  qmi8658b_driver_read_fifo
**入口参数:  driver   ---        驱动上下文（输入）
**           max_len  ---        输出缓冲区容量（输入）
**出口参数:  data     ---        FIFO 原始字节流
**           len      ---        实际读取字节数
**函数功能:  请求并读取当前 FIFO 数据
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_read_fifo(qmi8658b_driver_t *driver, uint8_t *data, uint16_t max_len, uint16_t *len)
{
    uint8_t count[2];
    uint8_t fifo_ctrl;
    uint16_t fifo_words;
    uint16_t fifo_len;
    int ret;

    if ((data == NULL) || (len == NULL) || (max_len == 0U))
    {
        return -EINVAL;
    }

    if ((driver == NULL) || (driver->fifo_sensor_enable == 0U))
    {
        return -EINVAL;
    }

    ret = driver->bus.read(driver->bus.context, QMI8658B_REG_FIFO_COUNT, count, sizeof(count));
    if (ret != 0)
    {
        return ret;
    }

    // FIFO 计数为 10 位，单位字(2字节)。count[0]=低8位, count[1] bits[1:0]=高2位
    fifo_words = (uint16_t)((uint16_t)count[0] | (((uint16_t)count[1] & 0x03U) << 8));
    if (fifo_words > QMI8658B_FIFO_MAX_WORDS)
    {
        return -EIO;
    }

    fifo_len = (uint16_t)(fifo_words * 2U);

    if (fifo_len > max_len)
    {
        // FIFO 可保留未读取的剩余数据，本次仅读取调用方缓冲区可容纳的数据
        fifo_len = max_len;
    }

    *len = fifo_len;
    if (fifo_len == 0U)
    {
        return 0;
    }

    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_FIFO_CTRL, &fifo_ctrl);
    if (ret != 0)
    {
        return ret;
    }

    // 数据手册 §8.8: CTRL_CMD_REQ_FIFO 会自动设置 FIFO_RD_MODE(bit7), 不需要手动置位
    ret = qmi8658b_driver_send_command(driver, QMI8658B_CMD_REQUEST_FIFO);
    if (ret != 0)
    {
        return ret;
    }

    ret = driver->bus.read(driver->bus.context, QMI8658B_REG_FIFO_DATA, data, fifo_len);
    if (qmi8658b_driver_write_u8(driver, QMI8658B_REG_FIFO_CTRL, fifo_ctrl) != 0)
    {
        return (ret == 0) ? -EIO : ret;
    }

    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_flush_fifo
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  无
**函数功能:  清空芯片 FIFO 数据
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_flush_fifo(qmi8658b_driver_t *driver)
{
    return qmi8658b_driver_send_command(driver, QMI8658B_CMD_RESET_FIFO);
}

/********************************************************************
**函数名称:  qmi8658b_driver_get_fifo_status
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  status   ---        FIFO 状态寄存器值
**函数功能:  读取 FIFO_STATUS 寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_get_fifo_status(qmi8658b_driver_t *driver, uint8_t *status)
{
    if ((driver == NULL) || (status == NULL))
    {
        return -EINVAL;
    }

    return qmi8658b_driver_read_u8(driver, QMI8658B_REG_FIFO_STATUS, status);
}

/********************************************************************
**函数名称:  qmi8658b_driver_feature_enable
**入口参数:  driver   ---        驱动上下文（输入）
**           feature  ---        特性索引（输入）
**           enable   ---        true 使能，false 禁用（输入）
**出口参数:  无
**函数功能:  使能或禁用运动和敲击特性
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_feature_enable(qmi8658b_driver_t *driver, uint8_t feature, bool enable)
{
    uint8_t mask;
    uint8_t ctrl8;
    int ret;

    if (driver == NULL)
    {
        return -EINVAL;
    }

    if (feature > QMI8658B_FEATURE_TAP)
    {
        return -EINVAL;
    }

    if (enable && ((feature == QMI8658B_FEATURE_TAP) ? !driver->tap_configured : !driver->motion_configured))
    {
        return -EINVAL;
    }

    mask = (feature == QMI8658B_FEATURE_ANY_MOTION) ? QMI8658B_CTRL8_ANY_MOTION_ENABLE :
           (feature == QMI8658B_FEATURE_NO_MOTION) ? QMI8658B_CTRL8_NO_MOTION_ENABLE :
           (feature == QMI8658B_FEATURE_SIG_MOTION) ? QMI8658B_CTRL8_SIG_MOTION_ENABLE :
           QMI8658B_CTRL8_TAP_ENABLE;
    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL8, &ctrl8);
    if (ret != 0)
    {
        return ret;
    }

    // 数据手册 §9.1.3: Sig-Motion 依赖 Any-Motion 和 No-Motion 同时启用
    if (enable && (feature == QMI8658B_FEATURE_SIG_MOTION))
    {
        if ((ctrl8 & (QMI8658B_CTRL8_ANY_MOTION_ENABLE | QMI8658B_CTRL8_NO_MOTION_ENABLE)) !=
            (QMI8658B_CTRL8_ANY_MOTION_ENABLE | QMI8658B_CTRL8_NO_MOTION_ENABLE))
        {
            return -EINVAL;
        }
    }

    ctrl8 = enable ? (uint8_t)(ctrl8 | mask) : (uint8_t)(ctrl8 & ~mask);
    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL8, ctrl8);
    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_set_motion_config
**入口参数:  driver      ---      驱动上下文（输入）
**           config_set1 ---      运动检测第一组 CAL 参数（输入）
**           config_set2 ---      运动检测第二组 CAL 参数（输入）
**出口参数:  无
**函数功能:  配置任意运动、静止和显著运动检测参数
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_motion_config(qmi8658b_driver_t *driver, const uint8_t config_set1[8], const uint8_t config_set2[8])
{
    int ret;

    ret = qmi8658b_driver_config_engine(driver, config_set1, config_set2, QMI8658B_CMD_MOTION);
    if (ret == 0)
    {
        driver->motion_configured = true;
    }

    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_set_tap_config
**入口参数:  driver      ---      驱动上下文（输入）
**           config_set1 ---      敲击检测第一组 CAL 参数（输入）
**           config_set2 ---      敲击检测第二组 CAL 参数（输入）
**出口参数:  无
**函数功能:  配置单击和双击检测参数
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_tap_config(qmi8658b_driver_t *driver, const uint8_t config_set1[8], const uint8_t config_set2[8])
{
    int ret;

    ret = qmi8658b_driver_config_engine(driver, config_set1, config_set2, QMI8658B_CMD_ENABLE_TAP);
    if (ret == 0)
    {
        driver->tap_configured = true;
    }

    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_get_tap_status
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  status   ---        TAP_STATUS 寄存器值
**函数功能:  读取敲击检测结果
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_get_tap_status(qmi8658b_driver_t *driver, uint8_t *status)
{
    if ((driver == NULL) || (status == NULL))
    {
        return -EINVAL;
    }

    return qmi8658b_driver_read_u8(driver, QMI8658B_REG_TAP_STATUS, status);
}

/********************************************************************
**函数名称:  qmi8658b_driver_get_motion_status
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  status   ---        运动检测实时状态
**函数功能:  读取 STATUS1 寄存器解析运动检测状态位
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_get_motion_status(qmi8658b_driver_t *driver, qmi8658b_motion_status_t *status)
{
    uint8_t raw;
    int ret;

    if ((driver == NULL) || (status == NULL))
    {
        return -EINVAL;
    }

    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_STATUS1, &raw);
    if (ret != 0)
    {
        return ret;
    }

    status->any_motion = ((raw & QMI8658B_STATUS1_ANY_MOTION) != 0U);
    status->no_motion  = ((raw & QMI8658B_STATUS1_NO_MOTION) != 0U);
    status->sig_motion = ((raw & QMI8658B_STATUS1_SIG_MOTION) != 0U);
    status->tap        = ((raw & QMI8658B_STATUS1_TAP) != 0U);

    return 0;
}

/********************************************************************
**函数名称:  qmi8658b_driver_set_sync_sample
**入口参数:  driver   ---        驱动上下文（输入）
**           enable   ---        true 使能同步采样，false 禁用
**出口参数:  无
**函数功能:  配置同步采样锁定读取和 AHB 时钟门控
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_sync_sample(qmi8658b_driver_t *driver, bool enable)
{
    uint8_t ctrl7;
    uint8_t ahb_clock_gating;
    int ret;

    if (driver == NULL)
    {
        return -EINVAL;
    }

    if (enable)
    {
        ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_FIFO_CTRL, 0U);
        if (ret != 0)
        {
            return ret;
        }

        ahb_clock_gating = 0x01U;
        ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CAL1_L, ahb_clock_gating);
        if (ret != 0)
        {
            return ret;
        }

        ret = qmi8658b_driver_send_command(driver, QMI8658B_CMD_AHB_CLOCK_GATING);
        if (ret != 0)
        {
            return ret;
        }
    }

    if (!enable)
    {
        // 数据手册要求退出锁定读取前先关闭传感器，再恢复 AHB 时钟门控
        ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, 0U);
        if (ret != 0)
        {
            return ret;
        }

        ahb_clock_gating = 0U;
        ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CAL1_L, ahb_clock_gating);
        if (ret != 0)
        {
            return ret;
        }

        ret = qmi8658b_driver_send_command(driver, QMI8658B_CMD_AHB_CLOCK_GATING);
        if (ret != 0)
        {
            return ret;
        }
    }

    ctrl7 = (uint8_t)(driver->config.sensor_enable | (enable ? QMI8658B_CTRL7_SYNC_SAMPLE : 0U));
    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, ctrl7);
    if (ret == 0)
    {
        driver->sync_sample = enable;
    }

    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_get_chip_info
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  info     ---        固件版本和芯片唯一标识
**函数功能:  刷新并读取芯片扩展信息
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_get_chip_info(qmi8658b_driver_t *driver, qmi8658b_chip_info_t *info)
{
    int ret;

    if ((driver == NULL) || (info == NULL))
    {
        return -EINVAL;
    }

    ret = qmi8658b_driver_send_command(driver, QMI8658B_CMD_COPY_USID);
    if (ret != 0)
    {
        return ret;
    }

    ret = driver->bus.read(driver->bus.context, QMI8658B_REG_DQW_L, info->firmware_version, 3U);
    if (ret != 0)
    {
        return ret;
    }

    return driver->bus.read(driver->bus.context, QMI8658B_REG_DVX_L, info->usid, 6U);
}

/********************************************************************
**函数名称:  qmi8658b_driver_set_offset
**入口参数:  driver   ---        驱动上下文（输入）
**           offset   ---        三轴偏置原始值（输入）
**           command  ---        偏置设置 CTRL9 命令（输入）
**出口参数:  无
**函数功能:  写入三轴主机偏置并提交到芯片
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
static int qmi8658b_driver_set_offset(qmi8658b_driver_t *driver, const int16_t offset[3], uint8_t command)
{
    uint8_t data[6];
    uint8_t index;
    int ret;

    if ((driver == NULL) || (offset == NULL))
    {
        return -EINVAL;
    }

    for (index = 0U; index < 3U; index++)
    {
        data[index * 2U] = (uint8_t)((uint16_t)offset[index] & 0x00FFU);
        data[index * 2U + 1U] = (uint8_t)(((uint16_t)offset[index] >> 8) & 0x00FFU);
    }

    ret = driver->bus.write(driver->bus.context, QMI8658B_REG_CAL1_L, data, sizeof(data));
    if (ret != 0)
    {
        return ret;
    }

    return qmi8658b_driver_send_command(driver, command);
}

/********************************************************************
**函数名称:  qmi8658b_driver_set_acc_offset
**入口参数:  driver   ---        驱动上下文（输入）
**           offset   ---        三轴加速度计偏置，格式 signed 4.12（输入）
**出口参数:  无
**函数功能:  设置加速度计主机偏置
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_acc_offset(qmi8658b_driver_t *driver, const int16_t offset[3])
{
    return qmi8658b_driver_set_offset(driver, offset, QMI8658B_CMD_ACC_OFFSET);
}

/********************************************************************
**函数名称:  qmi8658b_driver_set_gyr_offset
**入口参数:  driver   ---        驱动上下文（输入）
**           offset   ---        三轴陀螺仪偏置，格式 signed 11.5（输入）
**出口参数:  无
**函数功能:  设置陀螺仪主机偏置
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_gyr_offset(qmi8658b_driver_t *driver, const int16_t offset[3])
{
    return qmi8658b_driver_set_offset(driver, offset, QMI8658B_CMD_GYR_OFFSET);
}

/********************************************************************
**函数名称:  qmi8658b_driver_run_calibration
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  gain     ---        校准后的陀螺仪增益数据
**函数功能:  执行芯片片内按需校准 (COD)
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_run_calibration(qmi8658b_driver_t *driver, uint8_t gain[6])
{
    uint8_t ctrl7;
    uint8_t status;
    uint32_t count;
    int restore_ret;
    int ret;

    if ((driver == NULL) || (gain == NULL))
    {
        return -EINVAL;
    }

    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL7, &ctrl7);
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, 0U);
    if (ret != 0)
    {
        return ret;
    }

    // 直接写 CTRL9 校准命令（不走 send_command，因为校准耗时远超常规超时）
    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL9, QMI8658B_CMD_ON_DEMAND_CALI);
    if (ret != 0)
    {
        goto restore;
    }

    // 校准由芯片内部执行，需等待数据手册规定的完成时间（约 1.5s，留 2.2s 安全裕量）
    driver->bus.delay_ms(QMI8658B_CALIBRATION_TIME_MS);

    // CTRL9 协议阶段1: 确认 STATUSINT.bit7 已置位
    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_STATUS_INT, &status);
    if ((ret != 0) || ((status & QMI8658B_STATUS_INT_CMD_DONE) == 0U))
    {
        ret = (ret != 0) ? ret : -ETIMEDOUT;
        goto restore;
    }

    // CTRL9 协议阶段2: 写 ACK 并等待 STATUSINT.bit7 清零
    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL9, QMI8658B_CMD_ACK);
    if (ret != 0)
    {
        goto restore;
    }

    for (count = 0U; count < QMI8658B_COMMAND_TIMEOUT_MS; count++)
    {
        ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_STATUS_INT, &status);
        if (ret != 0)
        {
            goto restore;
        }

        if ((status & QMI8658B_STATUS_INT_CMD_DONE) == 0U)
        {
            break;
        }

        driver->bus.delay_ms(1U);
    }

    if (count >= QMI8658B_COMMAND_TIMEOUT_MS)
    {
        ret = -ETIMEDOUT;
        goto restore;
    }

    // 检查 COD 状态
    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_COD_STATUS, &status);
    if ((ret != 0) || (status != 0U))
    {
        ret = (ret != 0) ? ret : -EIO;
        goto restore;
    }

    ret = driver->bus.read(driver->bus.context, QMI8658B_REG_DVX_L, gain, 6U);

restore:
    restore_ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, ctrl7);
    if ((ret == 0) && (restore_ret != 0))
    {
        ret = restore_ret;
    }

    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_apply_gyro_gain
**入口参数:  driver   ---        驱动上下文（输入）
**           gain     ---        陀螺仪增益数据（输入）
**出口参数:  无
**函数功能:  写入并应用陀螺仪校准增益
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_apply_gyro_gain(qmi8658b_driver_t *driver, const uint8_t gain[6])
{
    uint8_t ctrl7;
    int restore_ret;
    int ret;

    if ((driver == NULL) || (gain == NULL))
    {
        return -EINVAL;
    }

    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL7, &ctrl7);
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, 0U);
    if (ret != 0)
    {
        return ret;
    }

    ret = driver->bus.write(driver->bus.context, QMI8658B_REG_CAL1_L, gain, 6U);
    if (ret != 0)
    {
        goto restore;
    }

    ret = qmi8658b_driver_send_command(driver, QMI8658B_CMD_APPLY_GYRO_GAIN);

restore:
    restore_ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, ctrl7);
    if ((ret == 0) && (restore_ret != 0))
    {
        ret = restore_ret;
    }

    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_run_self_test
**入口参数:  driver       ---        驱动上下文（输入）
**           sensor_mask ---        自检传感器使能位（输入）
**出口参数:  result       ---        自检结果与输出值
**函数功能:  触发加速度计/陀螺仪硬件自检并判定结果
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_run_self_test(qmi8658b_driver_t *driver, uint8_t sensor_mask, qmi8658b_self_test_result_t *result)
{
    uint8_t reg_data[6];
    uint8_t ctrl2;
    uint8_t ctrl3;
    uint8_t ctrl7;
    uint8_t status;
    uint32_t timeout;
    int restore_ret;
    int16_t raw[3];
    int ret;

    if ((driver == NULL) || (sensor_mask == 0U) || ((sensor_mask & ~0x03U) != 0U) || (result == NULL))
    {
        return -EINVAL;
    }

    memset(result, 0, sizeof(*result));

    // 保存当前配置后禁用传感器
    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL2, &ctrl2);
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL3, &ctrl3);
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL7, &ctrl7);
    if (ret != 0)
    {
        return ret;
    }

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, 0U);
    if (ret != 0)
    {
        return ret;
    }

    // 加速度计自检: 设置 aST=1(bit7) + aODR=250Hz(0x05)，芯片自动选择 16g 量程
    if ((sensor_mask & QMI8658B_CTRL7_ACC_ENABLE) != 0U)
    {
        ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL2, 0x80U | 0x05U);    // bit7=自检使能, aODR=0101=250Hz
        if (ret != 0)
        {
            goto restore;
        }

        // 等待自检完成: STATUSINT.bit0 置位
        for (timeout = 0U; timeout < 1000U; timeout++)
        {
            ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_STATUS_INT, &status);
            if ((ret == 0) && ((status & 0x01U) != 0U))
            {
                break;
            }

            driver->bus.delay_ms(1U);
        }

        if (timeout >= 1000U)
        {
            ret = -ETIMEDOUT;
            goto restore;
        }

        // 恢复 CTRL2 原始值以清除自检位
        ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL2, ctrl2);
        if (ret != 0)
        {
            goto restore;
        }

        // 等待 STATUSINT.bit0 清零确认自检结束
        for (timeout = 0U; timeout < 1000U; timeout++)
        {
            ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_STATUS_INT, &status);
            if ((ret == 0) && ((status & 0x01U) == 0U))
            {
                break;
            }

            driver->bus.delay_ms(1U);
        }

        if (timeout >= 1000U)
        {
            ret = -ETIMEDOUT;
            goto restore;
        }

        // 读取自检输出 DVX_L ~ DVZ_H (0x51 ~ 0x56)
        ret = driver->bus.read(driver->bus.context, QMI8658B_REG_DVX_L, reg_data, sizeof(reg_data));
        if (ret != 0)
        {
            goto restore;
        }

        raw[0] = (int16_t)((uint16_t)reg_data[0] | ((uint16_t)reg_data[1] << 8));
        raw[1] = (int16_t)((uint16_t)reg_data[2] | ((uint16_t)reg_data[3] << 8));
        raw[2] = (int16_t)((uint16_t)reg_data[4] | ((uint16_t)reg_data[5] << 8));
        // 自检灵敏度 2048 LSB/g（数据手册: signed U5.11, 0.5mg/LSB）→ 换算为 mg
        result->acc_x_mg = (int32_t)((int32_t)raw[0] * 1000 / 2048);
        result->acc_y_mg = (int32_t)((int32_t)raw[1] * 1000 / 2048);
        result->acc_z_mg = (int32_t)((int32_t)raw[2] * 1000 / 2048);

        // 判断依据：三轴输出绝对值均 > 200mg（数据手册要求）
        result->acc_pass = (result->acc_x_mg > QMI8658B_ST_ACC_THRESHOLD_MG ||
                            result->acc_x_mg < -QMI8658B_ST_ACC_THRESHOLD_MG) &&
                           (result->acc_y_mg > QMI8658B_ST_ACC_THRESHOLD_MG ||
                            result->acc_y_mg < -QMI8658B_ST_ACC_THRESHOLD_MG) &&
                           (result->acc_z_mg > QMI8658B_ST_ACC_THRESHOLD_MG ||
                            result->acc_z_mg < -QMI8658B_ST_ACC_THRESHOLD_MG);
    }

    // 陀螺仪自检: 自检前确保 CTRL7=0（传感器禁用）
    if ((sensor_mask & QMI8658B_CTRL7_GYR_ENABLE) != 0U)
    {
        ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, 0U);
        if (ret != 0)
        {
            goto restore;
        }

        ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL3, 0x80U | 0x05U);    // gST=1(bit7) + gODR=250Hz(0x05)
        if (ret != 0)
        {
            goto restore;
        }

        // 等待自检完成: STATUSINT.bit0 置位
        for (timeout = 0U; timeout < 1000U; timeout++)
        {
            ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_STATUS_INT, &status);
            if ((ret == 0) && ((status & 0x01U) != 0U))
            {
                break;
            }

            driver->bus.delay_ms(1U);
        }

        if (timeout >= 1000U)
        {
            ret = -ETIMEDOUT;
            goto restore;
        }

        // 恢复 CTRL3 原始值以清除自检位
        ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL3, ctrl3);
        if (ret != 0)
        {
            goto restore;
        }

        // 等待 STATUSINT.bit0 清零确认自检结束
        for (timeout = 0U; timeout < 1000U; timeout++)
        {
            ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_STATUS_INT, &status);
            if ((ret == 0) && ((status & 0x01U) == 0U))
            {
                break;
            }

            driver->bus.delay_ms(1U);
        }

        if (timeout >= 1000U)
        {
            ret = -ETIMEDOUT;
            goto restore;
        }

        ret = driver->bus.read(driver->bus.context, QMI8658B_REG_DVX_L, reg_data, sizeof(reg_data));
        if (ret != 0)
        {
            goto restore;
        }

        raw[0] = (int16_t)((uint16_t)reg_data[0] | ((uint16_t)reg_data[1] << 8));
        raw[1] = (int16_t)((uint16_t)reg_data[2] | ((uint16_t)reg_data[3] << 8));
        raw[2] = (int16_t)((uint16_t)reg_data[4] | ((uint16_t)reg_data[5] << 8));
        // 自检灵敏度 16 LSB/dps（数据手册: signed U12.4, 62.5mdps/LSB）→ 换算为 mdps
        result->gyr_x_mdps = (int32_t)((int32_t)raw[0] * 1000 / 16);
        result->gyr_y_mdps = (int32_t)((int32_t)raw[1] * 1000 / 16);
        result->gyr_z_mdps = (int32_t)((int32_t)raw[2] * 1000 / 16);

        // 判断依据：三轴输出绝对值均 > 300dps = 300000mdps（数据手册要求）
        result->gyr_pass = (result->gyr_x_mdps > QMI8658B_ST_GYR_THRESHOLD_MDPS ||
                            result->gyr_x_mdps < -QMI8658B_ST_GYR_THRESHOLD_MDPS) &&
                           (result->gyr_y_mdps > QMI8658B_ST_GYR_THRESHOLD_MDPS ||
                            result->gyr_y_mdps < -QMI8658B_ST_GYR_THRESHOLD_MDPS) &&
                           (result->gyr_z_mdps > QMI8658B_ST_GYR_THRESHOLD_MDPS ||
                            result->gyr_z_mdps < -QMI8658B_ST_GYR_THRESHOLD_MDPS);
    }

restore:
    // 恢复原始配置
    restore_ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, 0U);
    if (restore_ret == 0)
    {
        restore_ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL2, ctrl2);
    }

    if (restore_ret == 0)
    {
        restore_ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL3, ctrl3);
    }

    if (restore_ret == 0)
    {
        restore_ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, ctrl7);
    }

    if ((ret == 0) && (restore_ret != 0))
    {
        ret = restore_ret;
    }

    return ret;
}

/********************************************************************
**函数名称:  qmi8658b_driver_set_power_mode
**入口参数:  driver       ---      驱动上下文（输入）
**           sensor_enable ---     CTRL7 传感器使能位（输入）
**           gyro_snooze  ---      true 启用陀螺仪休眠模式（输入）
**           power_down   ---      true 启用完全掉电模式（输入）
**出口参数:  无
**函数功能:  配置传感器电源模式
**           gyro_snooze: gSN=1，仅保持陀螺仪驱动，检测电路关闭
**           power_down:  CTRL1.SensorDisable=1，关闭内部高速时钟
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_power_mode(qmi8658b_driver_t *driver, uint8_t sensor_enable, bool gyro_snooze, bool power_down)
{
    uint8_t ctrl1;
    uint8_t ctrl7;
    int ret;

    if (driver == NULL)
    {
        return -EINVAL;
    }

    // 配置完全掉电: 置位 CTRL1.bit0 关闭内部高速时钟
    ret = qmi8658b_driver_read_u8(driver, QMI8658B_REG_CTRL1, &ctrl1);
    if (ret != 0)
    {
        return ret;
    }

    if (power_down)
    {
        ctrl1 = (uint8_t)(ctrl1 | QMI8658B_CTRL1_SENSOR_DISABLE);
    }
    else
    {
        ctrl1 = (uint8_t)(ctrl1 & ~QMI8658B_CTRL1_SENSOR_DISABLE);
    }

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL1, ctrl1);
    if (ret != 0)
    {
        return ret;
    }

    // 配置传感器使能和陀螺仪休眠
    if (gyro_snooze && ((sensor_enable & QMI8658B_CTRL7_GYR_ENABLE) != 0U))
    {
        // gSN=1 且 gEN=1：陀螺仪仅保持驱动，检测电路关闭以降低功耗
        ctrl7 = (uint8_t)(sensor_enable | QMI8658B_CTRL7_GYR_SNOOZE);
    }
    else
    {
        // gSN=0：陀螺仪正常工作模式
        ctrl7 = (uint8_t)(sensor_enable & ~QMI8658B_CTRL7_GYR_SNOOZE);
    }

    ret = qmi8658b_driver_write_u8(driver, QMI8658B_REG_CTRL7, ctrl7);
    if (ret != 0)
    {
        return ret;
    }

    driver->config.sensor_enable = sensor_enable;
    return 0;
}
