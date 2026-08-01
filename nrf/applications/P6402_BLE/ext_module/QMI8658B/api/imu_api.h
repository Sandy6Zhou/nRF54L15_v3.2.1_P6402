/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        imu_api.h
**文件描述:        QMI8658B 六轴传感器统一接口头文件
**当前版本:        V1.0
**作    者:        周森达 (zhousenda@jimiiot.com)
**完成日期:        2026.07.30
*********************************************************************
** 功能描述:        定义 QMI8658B 初始化、数据、FIFO、中断、特性和校准统一接口
*********************************************************************/

#ifndef _IMU_API_H_
#define _IMU_API_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    IMU_SUCCESS = 0,
    IMU_ERROR_INIT,
    IMU_ERROR_COMM,
    IMU_ERROR_CHIP_ID,
    IMU_ERROR_PARAM,
    IMU_ERROR_TIMEOUT,
    IMU_ERROR_NOT_SUPPORTED,
    IMU_ERROR_SELF_TEST,
} imu_result_t;

typedef enum
{
    IMU_ACC_RANGE_2G = 0,
    IMU_ACC_RANGE_4G,
    IMU_ACC_RANGE_8G,
    IMU_ACC_RANGE_16G,
    IMU_ACC_RANGE_MAX,
} imu_acc_range_t;

typedef enum
{
    IMU_GYR_RANGE_16DPS = 0,
    IMU_GYR_RANGE_32DPS,
    IMU_GYR_RANGE_64DPS,
    IMU_GYR_RANGE_128DPS,
    IMU_GYR_RANGE_256DPS,
    IMU_GYR_RANGE_512DPS,
    IMU_GYR_RANGE_1024DPS,
    IMU_GYR_RANGE_2048DPS,
    IMU_GYR_RANGE_MAX,
} imu_gyr_range_t;

/* ODR 枚举值直接对应 CTRL2.aODR / CTRL3.gODR 寄存器位值 */
typedef enum
{
    IMU_ODR_7174HZ   = 0x00,   /* 6DOF 模式专用，单传感器不可用 */
    IMU_ODR_3587HZ   = 0x01,   /* 6DOF 模式专用，单传感器不可用 */
    IMU_ODR_1793HZ   = 0x02,   /* 6DOF 模式专用，单传感器不可用 */
    IMU_ODR_1000HZ   = 0x03,   /* 加速度计 1000Hz / 陀螺仪 896.8Hz */
    IMU_ODR_500HZ    = 0x04,   /* 加速度计 500Hz / 陀螺仪 448.4Hz */
    IMU_ODR_250HZ    = 0x05,   /* 加速度计 250Hz / 陀螺仪 224.2Hz */
    IMU_ODR_125HZ    = 0x06,   /* 加速度计 125Hz / 陀螺仪 112.1Hz */
    IMU_ODR_62_5HZ   = 0x07,   /* 加速度计 62.5Hz / 陀螺仪 56.05Hz */
    IMU_ODR_31_25HZ  = 0x08,   /* 加速度计 31.25Hz / 陀螺仪 28.025Hz */
    IMU_ODR_128HZ_LP = 0x0C,   /* 加速度计低功耗 128Hz，仅加速度计可用 */
    IMU_ODR_21HZ_LP  = 0x0D,   /* 加速度计低功耗 21Hz，仅加速度计可用 */
    IMU_ODR_11HZ_LP  = 0x0E,   /* 加速度计低功耗 11Hz，仅加速度计可用 */
    IMU_ODR_3HZ_LP   = 0x0F,   /* 加速度计低功耗 3Hz，仅加速度计可用 */
} imu_odr_t;

typedef enum
{
    IMU_LPF_MODE_0 = 0,         /* 带宽 2.66% ODR，最强滤波 */
    IMU_LPF_MODE_1,             /* 带宽 3.63% ODR */
    IMU_LPF_MODE_2,             /* 带宽 5.39% ODR */
    IMU_LPF_MODE_3,             /* 带宽 13.37% ODR，最弱滤波 */
    IMU_LPF_MODE_MAX,
} imu_lpf_mode_t;

typedef enum
{
    IMU_POWER_NORMAL = 0,       /* 正常模式，加速度计和陀螺仪均工作 */
    IMU_POWER_LOW_POWER,        /* 低功耗模式，仅加速度计低速采样 */
    IMU_POWER_GYRO_SNOOZE,      /* 陀螺仪休眠模式，仅保持驱动，检测关闭 */
    IMU_POWER_SUSPEND,          /* 传感器关闭，高速时钟保持 */
    IMU_POWER_DOWN,             /* 完全掉电，所有功能块关闭 */
    IMU_POWER_MAX,
} imu_power_mode_t;

typedef enum
{
    IMU_INT_NONE = 0,
    IMU_INT_PIN1,
    IMU_INT_PIN_MAX,
} imu_int_pin_t;

typedef enum
{
    IMU_INT_SRC_FIFO_WATERMARK = 0, /* FIFO 水位到达阈值 */
    IMU_INT_SRC_ACTIVITY,           /* Any/No/Sig/Tap 共用活动检测中断 */
    IMU_INT_SRC_MAX,                /* 中断路由数量 */
} imu_int_src_t;

typedef enum
{
    IMU_FEATURE_ANY_MOTION = 0,     /* 任意运动检测 */
    IMU_FEATURE_NO_MOTION,          /* 静止检测 */
    IMU_FEATURE_SIG_MOTION,         /* 显著运动检测 */
    IMU_FEATURE_TAP,                /* 敲击检测 */
    IMU_FEATURE_MAX,                /* 特性数量 */
} imu_feature_t;

typedef enum
{
    IMU_FIFO_BYPASS = 0,            /* 旁路模式，不缓存数据 */
    IMU_FIFO_FIFO,                  /* FIFO 满后停止写入 */
    IMU_FIFO_STREAM,                /* FIFO 满后覆盖旧数据 */
} imu_fifo_mode_t;

typedef enum
{
    IMU_FIFO_SIZE_16  = 0,          /* 16 样本 */
    IMU_FIFO_SIZE_32  = 1,          /* 32 样本 */
    IMU_FIFO_SIZE_64  = 2,          /* 64 样本 */
    IMU_FIFO_SIZE_128 = 3,          /* 128 样本 */
} imu_fifo_size_t;

typedef struct
{
    int16_t acc_x;                   /* X 轴加速度原始计数 */
    int16_t acc_y;                   /* Y 轴加速度原始计数 */
    int16_t acc_z;                   /* Z 轴加速度原始计数 */
    int16_t gyr_x;                   /* X 轴陀螺仪原始计数 */
    int16_t gyr_y;                   /* Y 轴陀螺仪原始计数 */
    int16_t gyr_z;                   /* Z 轴陀螺仪原始计数 */
    int16_t temperature;             /* 温度原始计数 */
} imu_raw_data_t;

typedef struct
{
    int32_t acc_x;                   /* X 轴加速度，单位 mg */
    int32_t acc_y;                   /* Y 轴加速度，单位 mg */
    int32_t acc_z;                   /* Z 轴加速度，单位 mg */
    int32_t gyr_x;                   /* X 轴角速度，单位 mdps */
    int32_t gyr_y;                   /* Y 轴角速度，单位 mdps */
    int32_t gyr_z;                   /* Z 轴角速度，单位 mdps */
    int32_t temperature;             /* 温度，单位 0.01 摄氏度 */
} imu_data_t;

typedef struct
{
    imu_acc_range_t acc_range;       /* 加速度计量程 */
    imu_odr_t acc_odr;               /* 加速度计输出速率 */
    imu_gyr_range_t gyr_range;       /* 陀螺仪量程 */
    imu_odr_t gyr_odr;               /* 陀螺仪输出速率 */
    imu_power_mode_t power_mode;     /* 电源模式 */
    bool lpf_enable;                 /* 低通滤波使能 */
    imu_lpf_mode_t lpf_mode;         /* 低通滤波带宽模式 */
} imu_config_t;

typedef struct
{
    bool acc_enable;                 /* FIFO 写入加速度计数据 */
    bool gyr_enable;                 /* FIFO 写入陀螺仪数据 */
    imu_fifo_mode_t mode;            /* FIFO 工作模式 */
    imu_fifo_size_t fifo_size;       /* FIFO 大小 */
    uint8_t watermark;               /* FIFO 水印阈值 */
    imu_int_pin_t int_pin;           /* FIFO 水印中断输出引脚 */
} imu_fifo_config_t;

typedef struct
{
    uint8_t any_motion_threshold_x;  /* 任意运动 X 轴阈值，单位 1/32g */
    uint8_t any_motion_threshold_y;  /* 任意运动 Y 轴阈值，单位 1/32g */
    uint8_t any_motion_threshold_z;  /* 任意运动 Z 轴阈值，单位 1/32g */
    uint8_t no_motion_threshold_x;   /* 静止检测 X 轴阈值，单位 1/32g */
    uint8_t no_motion_threshold_y;   /* 静止检测 Y 轴阈值，单位 1/32g */
    uint8_t no_motion_threshold_z;   /* 静止检测 Z 轴阈值，单位 1/32g */
    uint8_t any_motion_window;       /* 任意运动连续样本数 */
    uint8_t no_motion_window;        /* 静止检测连续样本数 */
    uint16_t sig_motion_wait_window; /* 显著运动等待窗口，单位样本 */
    uint16_t sig_motion_confirm_window; /* 显著运动确认窗口，单位样本 */
    uint8_t mode_ctrl;               /* 运动检测轴使能和逻辑配置 */
} imu_motion_config_t;

typedef struct
{
    uint8_t peak_window;             /* 峰值检测窗口，单位样本 */
    uint8_t priority;                /* 敲击轴优先级 */
    uint16_t tap_window;             /* 单击后静默窗口，单位样本 */
    uint16_t double_tap_window;      /* 双击检测窗口，单位样本 */
    uint8_t alpha;                   /* 加速度平均系数，单位 1/128 */
    uint8_t gamma;                   /* 运动量平均系数，单位 1/128 */
    uint16_t peak_magnitude_threshold; /* 峰值阈值，单位 1/1024g² */
    uint16_t undefined_motion_threshold; /* 未定义运动阈值，单位 1/1024g² */
} imu_tap_config_t;

typedef struct
{
    uint8_t tap_number;              /* 1 表示单击，2 表示双击 */
    uint8_t axis;                    /* 1 表示 X，2 表示 Y，3 表示 Z */
    bool negative_polarity;          /* true 表示负方向敲击 */
} imu_tap_status_t;

typedef struct
{
    bool any_motion;                 /* 任意运动检测到 */
    bool no_motion;                  /* 静止检测到 */
    bool sig_motion;                 /* 显著运动检测到 */
    bool tap;                        /* 敲击检测到 */
} imu_motion_status_t;

typedef struct
{
    uint8_t firmware_version[3];     /* 固件版本，低字节在前 */
    uint8_t usid[6];                 /* 芯片唯一标识 */
} imu_chip_info_t;

typedef struct
{
    bool acc_pass;                   /* 加速度计自检通过 */
    bool gyr_pass;                   /* 陀螺仪自检通过 */
    int32_t acc_x_mg;                /* X 轴加速度计自检输出，单位 mg */
    int32_t acc_y_mg;                /* Y 轴加速度计自检输出，单位 mg */
    int32_t acc_z_mg;                /* Z 轴加速度计自检输出，单位 mg */
    int32_t gyr_x_mdps;              /* X 轴陀螺仪自检输出，单位 mdps */
    int32_t gyr_y_mdps;              /* Y 轴陀螺仪自检输出，单位 mdps */
    int32_t gyr_z_mdps;              /* Z 轴陀螺仪自检输出，单位 mdps */
} imu_self_test_result_t;

typedef void (*imu_int_callback_t)(void);

/********************************************************************
**函数名称:  imu_init
**入口参数:  config   ---        工作配置，NULL 使用默认配置（输入）
**出口参数:  无
**函数功能:  初始化六轴传感器
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_init(const imu_config_t *config);

/********************************************************************
**函数名称:  imu_get_chip_id
**入口参数:  无
**出口参数:  id       ---        芯片标识值
**函数功能:  读取芯片标识
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_get_chip_id(uint8_t *id);

/********************************************************************
**函数名称:  imu_set_config
**入口参数:  config   ---        工作配置（输入）
**出口参数:  无
**函数功能:  更新加速度计和陀螺仪配置
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_config(const imu_config_t *config);

/********************************************************************
**函数名称:  imu_set_power_mode
**入口参数:  mode     ---        电源模式（输入）
**出口参数:  无
**函数功能:  切换六轴传感器电源模式（正常/低功耗/休眠/掉电）
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_power_mode(imu_power_mode_t mode);

/********************************************************************
**函数名称:  imu_read_raw
**入口参数:  无
**出口参数:  raw      ---        原始六轴和温度数据
**函数功能:  读取未经换算的传感器数据
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_raw(imu_raw_data_t *raw);

/********************************************************************
**函数名称:  imu_read
**入口参数:  无
**出口参数:  data     ---        mg、mdps 和 0.01 摄氏度数据
**函数功能:  读取并换算六轴和温度数据
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read(imu_data_t *data);

/********************************************************************
**函数名称:  imu_read_temperature
**入口参数:  无
**出口参数:  temperature ---    0.01 摄氏度温度值
**函数功能:  读取换算后的温度数据
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_temperature(int32_t *temperature);

/********************************************************************
**函数名称:  imu_read_timestamp
**入口参数:  无
**出口参数:  timestamp ---      24 位传感器时间戳
**函数功能:  读取传感器时间戳
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_timestamp(uint32_t *timestamp);

/********************************************************************
**函数名称:  imu_read_status
**入口参数:  无
**出口参数:  status   ---        STATUS0 寄存器值
**函数功能:  读取普通状态寄存器
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_status(uint8_t *status);

/********************************************************************
**函数名称:  imu_read_int_status
**入口参数:  无
**出口参数:  status   ---        STATUSINT 寄存器值
**函数功能:  读取中断状态寄存器
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_int_status(uint8_t *status);

/********************************************************************
**函数名称:  imu_read_reg
**入口参数:  reg_addr ---        寄存器地址（输入）
**           len      ---        读取长度（输入）
**出口参数:  data     ---        寄存器数据
**函数功能:  读取任意连续寄存器
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_read_reg(uint8_t reg_addr, uint8_t *data, uint16_t len);

/********************************************************************
**函数名称:  imu_write_reg
**入口参数:  reg_addr ---        寄存器地址（输入）
**           data     ---        写入数据（输入）
**           len      ---        写入长度（输入）
**出口参数:  无
**函数功能:  写入任意连续寄存器
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_write_reg(uint8_t reg_addr, const uint8_t *data, uint16_t len);

/********************************************************************
**函数名称:  imu_int_pin_enable
**入口参数:  pin      ---        中断引脚（输入）
**           enable   ---        true 使能，false 禁用（输入）
**出口参数:  无
**函数功能:  使能或禁用指定中断引脚的输出驱动
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_int_pin_enable(imu_int_pin_t pin, bool enable);

/********************************************************************
**函数名称:  imu_int_map
**入口参数:  src      ---        中断源（输入）
**           pin      ---        目标中断引脚（输入）
**出口参数:  无
**函数功能:  映射中断源到指定中断引脚
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_int_map(imu_int_src_t src, imu_int_pin_t pin);

/********************************************************************
**函数名称:  imu_register_int_callback
**入口参数:  callback ---        GPIO 中断回调函数指针（输入）
**出口参数:  无
**函数功能:  注册板级 INT1 引脚上升沿通知回调
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_register_int_callback(imu_int_callback_t callback);

/********************************************************************
**函数名称:  imu_fifo_config
**入口参数:  config   ---        FIFO 工作配置（输入）
**出口参数:  无
**函数功能:  配置 FIFO 数据源、大小、工作模式和水印中断
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_fifo_config(const imu_fifo_config_t *config);

/********************************************************************
**函数名称:  imu_fifo_read
**入口参数:  max_frames ---     输出帧缓冲区容量（输入）
**出口参数:  frames     ---     FIFO 原始数据帧数组
**           frame_count ---   实际读取的帧数量
**函数功能:  读取并解析 FIFO 中的六轴原始数据帧
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_fifo_read(imu_raw_data_t *frames, uint16_t max_frames, uint16_t *frame_count);

/********************************************************************
**函数名称:  imu_fifo_flush
**入口参数:  无
**出口参数:  无
**函数功能:  清空 FIFO 中所有缓存数据
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_fifo_flush(void);

/********************************************************************
**函数名称:  imu_fifo_get_status
**入口参数:  无
**出口参数:  status   ---        FIFO_STATUS 寄存器原始值
**函数功能:  读取 FIFO 状态寄存器（满/空/水印/溢出标志）
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_fifo_get_status(uint8_t *status);

/********************************************************************
**函数名称:  imu_feature_enable
**入口参数:  feature  ---        目标嵌入式特性索引（输入）
**           enable   ---        true 使能，false 禁用（输入）
**出口参数:  无
**函数功能:  使能或禁用运动检测和敲击检测特性
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_feature_enable(imu_feature_t feature, bool enable);

/********************************************************************
**函数名称:  imu_set_motion_config
**入口参数:  config   ---        运动检测参数配置（输入）
**出口参数:  无
**函数功能:  按数据手册两段 CTRL9 流程配置 Any/No/Sig-Motion 检测参数
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_motion_config(const imu_motion_config_t *config);

/********************************************************************
**函数名称:  imu_set_tap_config
**入口参数:  config   ---        敲击检测参数配置（输入）
**出口参数:  无
**函数功能:  按数据手册两段 CTRL9 流程配置单击/双击检测参数
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_tap_config(const imu_tap_config_t *config);

/********************************************************************
**函数名称:  imu_get_tap_status
**入口参数:  无
**出口参数:  status   ---        敲击次数、触发轴和极性方向
**函数功能:  读取并解析 TAP_STATUS 寄存器
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_get_tap_status(imu_tap_status_t *status);

/********************************************************************
**函数名称:  imu_get_motion_status
**入口参数:  无
**出口参数:  status   ---        运动检测各事件实时标志
**函数功能:  读取 STATUS1 寄存器解析 Any/No/Sig-Motion 和 Tap 状态
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_get_motion_status(imu_motion_status_t *status);

/********************************************************************
**函数名称:  imu_set_sync_sample
**入口参数:  enable   ---        true 使能同步采样，false 禁用（输入）
**出口参数:  无
**函数功能:  配置同步采样锁定读取模式及 AHB 时钟门控
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_sync_sample(bool enable);

/********************************************************************
**函数名称:  imu_get_chip_info
**入口参数:  无
**出口参数:  info     ---        固件版本和芯片唯一标识 (USID)
**函数功能:  刷新并读取 QMI8658B 固件版本和唯一序列号
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_get_chip_info(imu_chip_info_t *info);

/********************************************************************
**函数名称:  imu_set_acc_offset
**入口参数:  offset   ---        三轴加速度计偏置，格式 signed 4.12（输入）
**出口参数:  无
**函数功能:  设置加速度计主机偏置补偿值
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_acc_offset(const int16_t offset[3]);

/********************************************************************
**函数名称:  imu_set_gyr_offset
**入口参数:  offset   ---        三轴陀螺仪偏置，格式 signed 11.5（输入）
**出口参数:  无
**函数功能:  设置陀螺仪主机偏置补偿值
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_set_gyr_offset(const int16_t offset[3]);

/********************************************************************
**函数名称:  imu_run_calibration
**入口参数:  无
**出口参数:  gain     ---        COD 校准后的陀螺仪三轴增益数据
**函数功能:  执行芯片片内按需校准 (COD)
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_run_calibration(uint8_t gain[6]);

/********************************************************************
**函数名称:  imu_apply_gyro_gain
**入口参数:  gain     ---        已保存的陀螺仪校准增益数据（输入）
**出口参数:  无
**函数功能:  恢复并应用已保存的陀螺仪校准增益参数
**返回值:    IMU_SUCCESS 表示成功，其他表示错误码
*********************************************************************/
imu_result_t imu_apply_gyro_gain(const uint8_t gain[6]);

/********************************************************************
**函数名称:  imu_run_self_test
**入口参数:  sensor_mask ---    自检传感器选择位掩码（输入）
**出口参数:  result       ---    自检输出值和判定结果
**函数功能:  触发加速度计/陀螺仪硬件自检并判定功能是否正常
**返回值:    IMU_SUCCESS 表示自检通过，IMU_ERROR_SELF_TEST 表示未通过
*********************************************************************/
imu_result_t imu_run_self_test(uint8_t sensor_mask, imu_self_test_result_t *result);

#endif /* _IMU_API_H_ */
