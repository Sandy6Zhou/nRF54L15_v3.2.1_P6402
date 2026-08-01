/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        qmi8658b_driver.h
**文件描述:        QMI8658B 芯片驱动层接口头文件
**当前版本:        V1.0
**作    者:        周森达 (zhousenda@jimiiot.com)
**完成日期:        2026.07.30
*********************************************************************
** 功能描述:        定义 QMI8658B 芯片驱动层的数据结构、总线回调和寄存器操作接口
*********************************************************************/

#ifndef _QMI8658B_DRIVER_H_
#define _QMI8658B_DRIVER_H_

#include <stdbool.h>
#include <stdint.h>

typedef int (*qmi8658b_read_cb_t)(void *context, uint8_t reg_addr, uint8_t *data, uint16_t len);
typedef int (*qmi8658b_write_cb_t)(void *context, uint8_t reg_addr, const uint8_t *data, uint16_t len);
typedef void (*qmi8658b_delay_ms_cb_t)(uint32_t delay_ms);

typedef struct
{
    void *context;                     /* 总线访问上下文 */
    qmi8658b_read_cb_t read;           /* 寄存器读回调 */
    qmi8658b_write_cb_t write;         /* 寄存器写回调 */
    qmi8658b_delay_ms_cb_t delay_ms;   /* 毫秒级延时回调 */
} qmi8658b_bus_t;

typedef struct
{
    uint8_t acc_range;                 /* 当前加速度计测量范围设置 */
    uint8_t acc_odr;                   /* 当前加速度计采样频率设置 */
    uint8_t gyr_range;                 /* 当前陀螺仪测量范围设置 */
    uint8_t gyr_odr;                   /* 当前陀螺仪采样频率设置 */
    uint8_t sensor_enable;             /* CTRL7 传感器使能位 */
    uint8_t ctrl8_value;               /* CTRL8 特性和中断路由配置 */
    uint8_t lpf_mode;                  /* CTRL5 低通滤波模式 0~3 */
    bool lpf_enable;                   /* 加速度计和陀螺仪低通滤波使能 */
} qmi8658b_config_t;

typedef struct
{
    bool acc_pass;                     /* 加速度计自检通过 */
    bool gyr_pass;                     /* 陀螺仪自检通过 */
    int32_t acc_x_mg;                  /* X 轴加速度计自检输出，单位 mg */
    int32_t acc_y_mg;                  /* Y 轴加速度计自检输出，单位 mg */
    int32_t acc_z_mg;                  /* Z 轴加速度计自检输出，单位 mg */
    int32_t gyr_x_mdps;                /* X 轴陀螺仪自检输出，单位 mdps */
    int32_t gyr_y_mdps;                /* Y 轴陀螺仪自检输出，单位 mdps */
    int32_t gyr_z_mdps;                /* Z 轴陀螺仪自检输出，单位 mdps */
} qmi8658b_self_test_result_t;

typedef enum
{
    QMI8658B_FIFO_SIZE_16  = 0,
    QMI8658B_FIFO_SIZE_32  = 1,
    QMI8658B_FIFO_SIZE_64  = 2,
    QMI8658B_FIFO_SIZE_128 = 3,
} qmi8658b_fifo_size_t;

typedef struct
{
    bool any_motion;                   /* 任意运动检测到 */
    bool no_motion;                    /* 静止检测到 */
    bool sig_motion;                   /* 显著运动检测到 */
    bool tap;                          /* 敲击检测到 */
} qmi8658b_motion_status_t;

typedef struct
{
    qmi8658b_bus_t bus;                /* 平台总线访问接口 */
    qmi8658b_config_t config;          /* 当前芯片工作配置 */
    uint16_t acc_lsb_per_g;            /* 每 g 对应的原始计数 */
    uint16_t gyr_lsb_per_dps;          /* 每 dps 对应的原始计数 */
    bool sync_sample;                  /* 同步采样锁定读取模式 */
    bool motion_configured;            /* 运动检测参数已写入 */
    bool tap_configured;               /* 敲击检测参数已写入 */
    bool initialized;                  /* 芯片初始化完成标志 */
    uint8_t fifo_sensor_enable;        /* FIFO 数据源使能位 (与 CTRL7 低 2 位同格式) */
} qmi8658b_driver_t;

typedef struct
{
    uint8_t firmware_version[3];       /* 固件版本，低字节在前 */
    uint8_t usid[6];                   /* 芯片唯一标识 */
} qmi8658b_chip_info_t;

/********************************************************************
**函数名称:  qmi8658b_driver_init
**入口参数:  driver   ---        驱动上下文（输入）
**           bus      ---        总线访问接口（输入）
**           config   ---        芯片工作配置（输入）
**出口参数:  无
**函数功能:  初始化 QMI8658B 驱动
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_init(qmi8658b_driver_t *driver, const qmi8658b_bus_t *bus, const qmi8658b_config_t *config);

/********************************************************************
**函数名称:  qmi8658b_driver_get_id
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  id       ---        芯片标识值
**函数功能:  读取芯片标识
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_get_id(qmi8658b_driver_t *driver, uint8_t *id);

/********************************************************************
**函数名称:  qmi8658b_driver_set_config
**入口参数:  driver   ---        驱动上下文（输入）
**           config   ---        芯片工作配置（输入）
**出口参数:  无
**函数功能:  更新芯片工作配置
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_config(qmi8658b_driver_t *driver, const qmi8658b_config_t *config);

/********************************************************************
**函数名称:  qmi8658b_driver_read
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  data     ---        温度和六轴原始数据
**函数功能:  等待数据就绪后读取连续传感器数据
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_read(qmi8658b_driver_t *driver, int16_t data[7]);

/********************************************************************
**函数名称:  qmi8658b_driver_read_temperature_raw
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  temp_raw ---        温度原始数据补码值
**函数功能:  读取温度寄存器原始值
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_read_temperature_raw(qmi8658b_driver_t *driver, int16_t *temp_raw);

/********************************************************************
**函数名称:  qmi8658b_driver_read_timestamp
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  timestamp ---      传感器时间戳
**函数功能:  读取传感器时间戳
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_read_timestamp(qmi8658b_driver_t *driver, uint32_t *timestamp);

/********************************************************************
**函数名称:  qmi8658b_driver_read_reg
**入口参数:  driver   ---        驱动上下文（输入）
**           reg_addr ---        寄存器地址（输入）
**           len      ---        读取长度（输入）
**出口参数:  data     ---        寄存器数据
**函数功能:  读取连续寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_read_reg(qmi8658b_driver_t *driver, uint8_t reg_addr, uint8_t *data, uint16_t len);

/********************************************************************
**函数名称:  qmi8658b_driver_write_reg
**入口参数:  driver   ---        驱动上下文（输入）
**           reg_addr ---        寄存器地址（输入）
**           data     ---        写入数据（输入）
**           len      ---        写入长度（输入）
**出口参数:  无
**函数功能:  写入连续寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_write_reg(qmi8658b_driver_t *driver, uint8_t reg_addr, const uint8_t *data, uint16_t len);

/********************************************************************
**函数名称:  qmi8658b_driver_send_command
**入口参数:  driver   ---        驱动上下文（输入）
**           command  ---        控制命令（输入）
**出口参数:  无
**函数功能:  发送芯片内部控制命令
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_send_command(qmi8658b_driver_t *driver, uint8_t command);

/********************************************************************
**函数名称:  qmi8658b_driver_enable_int_pin
**入口参数:  driver   ---        驱动上下文（输入）
**           pin      ---        中断引脚编号，仅支持 1（输入）
**           enable   ---        true 使能，false 禁用（输入）
**出口参数:  无
**函数功能:  使能或禁用 INT1 引脚输出
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_enable_int_pin(qmi8658b_driver_t *driver, uint8_t pin, bool enable);

/********************************************************************
**函数名称:  qmi8658b_driver_map_interrupt
**入口参数:  driver   ---        驱动上下文（输入）
**           source   ---        中断源（输入）
**           pin      ---        目标引脚编号（输入）
**出口参数:  无
**函数功能:  映射中断源输出到 INT1 引脚
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_map_interrupt(qmi8658b_driver_t *driver, uint8_t source, uint8_t pin);

/********************************************************************
**函数名称:  qmi8658b_driver_config_fifo
**入口参数:  driver        ---     驱动上下文（输入）
**           sensor_enable ---     FIFO 数据源设置（输入）
**           mode          ---     FIFO 工作模式（输入）
**           fifo_size     ---     FIFO 大小（输入）
**           watermark     ---     水印阈值（输入）
**           pin           ---     中断引脚编号（输入）
**出口参数:  无
**函数功能:  配置芯片 FIFO
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_config_fifo(qmi8658b_driver_t *driver, uint8_t sensor_enable, uint8_t mode,
                                 qmi8658b_fifo_size_t fifo_size, uint8_t watermark, uint8_t pin);

/********************************************************************
**函数名称:  qmi8658b_driver_read_fifo
**入口参数:  driver   ---        驱动上下文（输入）
**           max_len  ---        输出缓冲区容量（输入）
**出口参数:  data     ---        FIFO 原始数据
**           len      ---        实际数据长度
**函数功能:  读取 FIFO 数据
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_read_fifo(qmi8658b_driver_t *driver, uint8_t *data, uint16_t max_len, uint16_t *len);

/********************************************************************
**函数名称:  qmi8658b_driver_flush_fifo
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  无
**函数功能:  清空 FIFO 数据
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_flush_fifo(qmi8658b_driver_t *driver);

/********************************************************************
**函数名称:  qmi8658b_driver_get_fifo_status
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  status   ---        FIFO 状态寄存器值
**函数功能:  读取 FIFO_STATUS 寄存器
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_get_fifo_status(qmi8658b_driver_t *driver, uint8_t *status);

/********************************************************************
**函数名称:  qmi8658b_driver_feature_enable
**入口参数:  driver   ---        驱动上下文（输入）
**           feature  ---        芯片特性索引（输入）
**           enable   ---        true 使能，false 禁用（输入）
**出口参数:  无
**函数功能:  控制运动检测和敲击特性
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_feature_enable(qmi8658b_driver_t *driver, uint8_t feature, bool enable);

/********************************************************************
**函数名称:  qmi8658b_driver_set_motion_config
**入口参数:  driver      ---      驱动上下文（输入）
**           config_set1 ---      运动检测第一组 CAL 参数（输入）
**           config_set2 ---      运动检测第二组 CAL 参数（输入）
**出口参数:  无
**函数功能:  配置运动检测参数
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_motion_config(qmi8658b_driver_t *driver, const uint8_t config_set1[8], const uint8_t config_set2[8]);

/********************************************************************
**函数名称:  qmi8658b_driver_set_tap_config
**入口参数:  driver      ---      驱动上下文（输入）
**           config_set1 ---      敲击检测第一组 CAL 参数（输入）
**           config_set2 ---      敲击检测第二组 CAL 参数（输入）
**出口参数:  无
**函数功能:  配置敲击检测参数
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_tap_config(qmi8658b_driver_t *driver, const uint8_t config_set1[8], const uint8_t config_set2[8]);

/********************************************************************
**函数名称:  qmi8658b_driver_get_tap_status
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  status   ---        TAP_STATUS 寄存器值
**函数功能:  读取敲击检测结果
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_get_tap_status(qmi8658b_driver_t *driver, uint8_t *status);

/********************************************************************
**函数名称:  qmi8658b_driver_get_motion_status
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  status   ---        运动检测实时状态
**函数功能:  读取 STATUS1 寄存器运动检测状态位
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_get_motion_status(qmi8658b_driver_t *driver, qmi8658b_motion_status_t *status);

/********************************************************************
**函数名称:  qmi8658b_driver_set_sync_sample
**入口参数:  driver   ---        驱动上下文（输入）
**           enable   ---        true 使能同步采样，false 禁用
**出口参数:  无
**函数功能:  配置同步采样锁定读取和 AHB 时钟门控
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_sync_sample(qmi8658b_driver_t *driver, bool enable);

/********************************************************************
**函数名称:  qmi8658b_driver_get_chip_info
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  info     ---        固件版本和芯片唯一标识
**函数功能:  刷新并读取芯片扩展信息
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_get_chip_info(qmi8658b_driver_t *driver, qmi8658b_chip_info_t *info);

/********************************************************************
**函数名称:  qmi8658b_driver_set_acc_offset
**入口参数:  driver   ---        驱动上下文（输入）
**           offset   ---        三轴加速度计偏置，格式 signed 4.12（输入）
**出口参数:  无
**函数功能:  设置加速度计主机偏置
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_acc_offset(qmi8658b_driver_t *driver, const int16_t offset[3]);

/********************************************************************
**函数名称:  qmi8658b_driver_set_gyr_offset
**入口参数:  driver   ---        驱动上下文（输入）
**           offset   ---        三轴陀螺仪偏置，格式 signed 11.5（输入）
**出口参数:  无
**函数功能:  设置陀螺仪主机偏置
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_gyr_offset(qmi8658b_driver_t *driver, const int16_t offset[3]);

/********************************************************************
**函数名称:  qmi8658b_driver_run_calibration
**入口参数:  driver   ---        驱动上下文（输入）
**出口参数:  gain     ---        校准后的陀螺仪增益数据
**函数功能:  执行芯片片内校准
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_run_calibration(qmi8658b_driver_t *driver, uint8_t gain[6]);

/********************************************************************
**函数名称:  qmi8658b_driver_apply_gyro_gain
**入口参数:  driver   ---        驱动上下文（输入）
**           gain     ---        陀螺仪校准增益数据（输入）
**出口参数:  无
**函数功能:  应用陀螺仪校准数据
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_apply_gyro_gain(qmi8658b_driver_t *driver, const uint8_t gain[6]);

/********************************************************************
**函数名称:  qmi8658b_driver_run_self_test
**入口参数:  driver       ---        驱动上下文（输入）
**           sensor_mask ---        自检传感器选择（输入）
**出口参数:  result       ---        自检输出结果
**函数功能:  触发芯片硬件自检并读取输出值
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_run_self_test(qmi8658b_driver_t *driver, uint8_t sensor_mask, qmi8658b_self_test_result_t *result);

/********************************************************************
**函数名称:  qmi8658b_driver_set_power_mode
**入口参数:  driver   ---        驱动上下文（输入）
**           mode     ---        电源模式（输入）
**出口参数:  无
**函数功能:  配置传感器电源模式（正常/休眠/掉电）
**           IMU_POWER_SUSPEND   传感器关闭，时钟保持
**           IMU_POWER_GYRO_SNOOZE 陀螺仪仅驱动保持
**           IMU_POWER_DOWN      全部掉电，时钟关闭
**返回值:    0 表示成功，负值表示失败
*********************************************************************/
int qmi8658b_driver_set_power_mode(qmi8658b_driver_t *driver, uint8_t sensor_enable, bool gyro_snooze, bool power_down);

#endif /* _QMI8658B_DRIVER_H_ */
