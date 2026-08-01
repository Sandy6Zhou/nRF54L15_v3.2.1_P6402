# QMI8658B 模块使用示例

模块路径：`ext_module/QMI8658B`

适用器件：QMI8658B 六轴惯性测量单元（3 轴加速度计、3 轴陀螺仪、温度与嵌入式运动检测）

公开头文件：`#include "imu_api.h"`

## 一、模块结构

```
QMI8658B/
├── api/
│   ├── imu_api.h              统一公开接口，应用层只需包含此文件
│   └── imu_api.c
├── driver/
│   ├── qmi8658b_driver.h      芯片驱动层接口
│   ├── qmi8658b_driver.c
│   └── qmi8658b_reg.h         寄存器与数据手册定义
├── port/
│   ├── qmi8658b_port.h        Zephyr I2C 和 INT1 适配层
│   └── qmi8658b_port.c
└── qmi8658b_usage_demo.md     本文档
```

应用层只调用 `imu_api.h` 中的接口。端口层固定使用设备树别名 `gsensor_i2c` 和 `gsensor_int`，I2C 从机地址为 `0x6B`；当前实现仅支持已接入 MCU 的 INT1。

## 二、返回值和数据单位

| 返回值 | 含义 |
|---|---|
| `IMU_SUCCESS` | 操作成功 |
| `IMU_ERROR_INIT` | 未初始化或端口初始化失败 |
| `IMU_ERROR_COMM` | I2C 通信失败 |
| `IMU_ERROR_CHIP_ID` | 芯片 ID 不匹配，QMI8658B 期望值为 `0x05` |
| `IMU_ERROR_PARAM` | 空指针、越界枚举或无效组合参数 |
| `IMU_ERROR_TIMEOUT` | 数据就绪或内部命令超时 |
| `IMU_ERROR_NOT_SUPPORTED` | 当前配置不支持该操作 |
| `IMU_ERROR_SELF_TEST` | 硬件自检未通过 |

`imu_data_t` 的加速度单位为 mg，`1000 mg = 1 g`；角速度单位为 mdps，`1000 mdps = 1 deg/s`；温度单位为 0.01 摄氏度，`2534 = 25.34 C`。`imu_raw_data_t` 提供未经换算的寄存器原始计数。

## 三、主要类型

| 类型 | 说明 |
|---|---|
| `imu_config_t` | 加速度、陀螺仪量程、ODR、电源模式与低通滤波配置 |
| `imu_data_t` / `imu_raw_data_t` | 换算后数据 / 原始数据 |
| `imu_fifo_config_t` | FIFO 数据源、模式、深度、水印与中断引脚配置 |
| `imu_motion_config_t` | Any-Motion、No-Motion、Sig-Motion 参数 |
| `imu_tap_config_t` | 单击和双击检测参数 |
| `imu_int_src_t` | `IMU_INT_SRC_FIFO_WATERMARK` 或 `IMU_INT_SRC_ACTIVITY` 中断路由 |
| `imu_chip_info_t` | 固件版本与六字节 USID |
| `imu_self_test_result_t` | 加速度计和陀螺仪自检结果及差分数据 |

量程枚举包括加速度 `IMU_ACC_RANGE_2G`、`4G`、`8G`、`16G`，陀螺仪 `IMU_GYR_RANGE_16DPS` 至 `IMU_GYR_RANGE_2048DPS`。常用 ODR 为 `IMU_ODR_1000HZ`、`500HZ`、`250HZ`、`125HZ`、`62_5HZ` 和 `31_25HZ`。低功耗 ODR `IMU_ODR_128HZ_LP` 至 `IMU_ODR_3HZ_LP` 仅适用于加速度计。

## 四、公开接口索引

| 功能 | 接口 |
|---|---|
| 初始化和识别 | `imu_init`、`imu_get_chip_id`、`imu_get_chip_info` |
| 配置和电源 | `imu_set_config`、`imu_set_power_mode` |
| 实时数据 | `imu_read_raw`、`imu_read`、`imu_read_temperature`、`imu_read_timestamp`、`imu_read_status` |
| 寄存器与状态 | `imu_read_int_status`、`imu_read_reg`、`imu_write_reg` |
| INT1 中断 | `imu_int_pin_enable`、`imu_int_map`、`imu_register_int_callback` |
| FIFO | `imu_fifo_config`、`imu_fifo_read`、`imu_fifo_flush`、`imu_fifo_get_status` |
| 运动和敲击 | `imu_set_motion_config`、`imu_set_tap_config`、`imu_feature_enable`、`imu_get_motion_status`、`imu_get_tap_status` |
| 同步与校准 | `imu_set_sync_sample`、`imu_set_acc_offset`、`imu_set_gyr_offset`、`imu_run_calibration`、`imu_apply_gyro_gain`、`imu_run_self_test` |

## 五、初始化和读取数据

调用任何其他接口前必须先执行 `imu_init`。传入 `NULL` 使用模块默认配置：加速度 ±8g/125Hz、陀螺仪 ±1024dps/125Hz、普通电源模式、LPF 模式 0。

```c
#include <string.h>

#include "imu_api.h"

static void app_imu_init(void)
{
    imu_config_t config;
    imu_result_t result;

    config.acc_range = IMU_ACC_RANGE_8G;
    config.acc_odr = IMU_ODR_125HZ;
    config.gyr_range = IMU_GYR_RANGE_1024DPS;
    config.gyr_odr = IMU_ODR_125HZ;
    config.power_mode = IMU_POWER_NORMAL;
    config.lpf_enable = true;
    config.lpf_mode = IMU_LPF_MODE_0;

    result = imu_init(&config);
    if (result != IMU_SUCCESS)
    {
        return;
    }
}
```

```c
static void app_imu_read(void)
{
    imu_data_t data;
    imu_result_t result;

    result = imu_read(&data);
    if (result != IMU_SUCCESS)
    {
        return;
    }

    /* data.acc_x 单位为 mg；data.gyr_x 单位为 mdps。 */
}
```

初始化后可通过 `imu_get_chip_id` 读取 ID，通过 `imu_get_chip_info` 读取 firmware 与 USID，通过 `imu_read_temperature` 读取独立温度值，通过 `imu_read_timestamp` 读取传感器内部时间戳。`imu_read_status` 读取 STATUS0 的加速度/陀螺仪数据就绪位，`imu_read_int_status` 读取 STATUSINT。

## 六、运行期配置和电源模式

使用 `imu_set_config` 可在运行时更新量程、ODR、LPF 和电源模式。`imu_set_power_mode` 仅切换电源状态，保留先前的量程和 ODR 配置。

```c
static void app_imu_set_power_mode(void)
{
    imu_result_t result;

    result = imu_set_power_mode(IMU_POWER_SUSPEND);
    if (result != IMU_SUCCESS)
    {
        return;
    }

    result = imu_set_power_mode(IMU_POWER_NORMAL);
    if (result != IMU_SUCCESS)
    {
        return;
    }
}
```

`IMU_POWER_LOW_POWER` 要求加速度 ODR 使用低功耗枚举，且该模式仅使能加速度计。`IMU_POWER_GYRO_SNOOZE`、`IMU_POWER_SUSPEND` 与 `IMU_POWER_DOWN` 的行为请以数据手册的 CTRL1/CTRL7 描述为准。

## 七、寄存器访问和 INT1 中断

`imu_read_reg` 和 `imu_write_reg` 用于调试或数据手册定义的高级配置。业务代码不应随意改写 CTRL1、CTRL7、CTRL8、CTRL9、FIFO 和 CAL 寄存器，否则会破坏 API 层维护的配置状态。读写寄存器时应严格使用数据手册定义的地址、长度和时序。

当前模块只支持 INT1：`imu_int_pin_enable` 的引脚参数必须为 `IMU_INT_PIN1`，`imu_int_map` 的目标也必须为 `IMU_INT_PIN1`。`IMU_INT_SRC_FIFO_WATERMARK` 独立映射 FIFO 水位中断；`IMU_INT_SRC_ACTIVITY` 统一映射 Any-Motion、No-Motion、Sig-Motion 和 Tap，四类活动事件不能独立映射，实际事件类型需由 `STATUS1` 判断。`imu_register_int_callback` 的回调在 GPIO 中断上下文运行，只能执行置位标志、计数或提交 work 等轻量操作，不能直接进行 I2C 读写、打印或长时间阻塞。

```c
static volatile bool s_imu_int_pending;

static void app_imu_int_callback(void)
{
    s_imu_int_pending = true;
}

static void app_imu_enable_fifo_interrupt(void)
{
    imu_result_t result;

    result = imu_register_int_callback(app_imu_int_callback);
    if (result != IMU_SUCCESS)
    {
        return;
    }

    result = imu_int_pin_enable(IMU_INT_PIN1, true);
    if (result != IMU_SUCCESS)
    {
        return;
    }

    result = imu_int_map(IMU_INT_SRC_FIFO_WATERMARK, IMU_INT_PIN1);
    if (result != IMU_SUCCESS)
    {
        return;
    }
}
```

```c
static void app_imu_enable_activity_interrupt(void)
{
    imu_result_t result;

    result = imu_register_int_callback(app_imu_int_callback);
    if (result != IMU_SUCCESS)
    {
        return;
    }

    result = imu_int_pin_enable(IMU_INT_PIN1, true);
    if (result != IMU_SUCCESS)
    {
        return;
    }

    result = imu_int_map(IMU_INT_SRC_ACTIVITY, IMU_INT_PIN1);
    if (result != IMU_SUCCESS)
    {
        return;
    }
}
```

收到回调后，应在普通线程上下文调用 `imu_read_int_status`、`imu_fifo_get_status` 或对应事件状态接口确认来源并处理数据。活动中断应使用 `imu_get_motion_status` 判断 Any/No/Sig 状态，Tap 事件再使用 `imu_get_tap_status` 读取次数、轴和极性。当前 API 不支持把源映射到 `IMU_INT_NONE` 解除路由；需要停止中断输出时调用 `imu_int_pin_enable(IMU_INT_PIN1, false)`。

## 八、FIFO 使用示例

FIFO 支持仅加速度、仅陀螺仪和六轴同时写入。六轴同时写入时，加速度与陀螺仪 ODR 必须相同。当前硬件仅有 INT1，因此 `int_pin` 必须使用 `IMU_INT_PIN1`。

```c
static void app_imu_read_fifo(void)
{
    imu_fifo_config_t config;
    imu_raw_data_t frames[16];
    imu_result_t result;
    uint16_t frame_count;

    config.acc_enable = true;
    config.gyr_enable = true;
    config.mode = IMU_FIFO_STREAM;
    config.fifo_size = IMU_FIFO_SIZE_128;
    config.watermark = 32U;
    config.int_pin = IMU_INT_PIN1;

    result = imu_fifo_config(&config);
    if (result != IMU_SUCCESS)
    {
        return;
    }

    result = imu_fifo_read(frames, 16U, &frame_count);
    if (result != IMU_SUCCESS)
    {
        return;
    }

    /* frames[0] 至 frames[frame_count - 1] 为原始 FIFO 数据。 */
}
```

使用 `imu_fifo_get_status` 可读取 FIFO 状态，使用 `imu_fifo_flush` 可清空缓存。同步采样与 FIFO 不应同时启用；需要同步读取时使用 `imu_set_sync_sample(true)`，完成后调用 `imu_set_sync_sample(false)`。

## 九、运动和敲击检测

运动检测必须先调用 `imu_set_motion_config` 写入参数，之后才能通过 `imu_feature_enable` 打开 `IMU_FEATURE_ANY_MOTION`、`IMU_FEATURE_NO_MOTION` 或 `IMU_FEATURE_SIG_MOTION`。显著运动要求 Any-Motion 与 No-Motion 已处于使能状态。敲击检测同理，先调用 `imu_set_tap_config`，再打开 `IMU_FEATURE_TAP`。

```c
static void app_imu_enable_any_motion(void)
{
    imu_motion_config_t config;
    imu_result_t result;

    memset(&config, 0, sizeof(config));
    config.any_motion_threshold_x = 3U;
    config.any_motion_threshold_y = 3U;
    config.any_motion_threshold_z = 3U;
    config.any_motion_window = 10U;
    config.no_motion_window = 10U;
    config.mode_ctrl = 0x3FU;

    result = imu_set_motion_config(&config);
    if (result != IMU_SUCCESS)
    {
        return;
    }

    result = imu_feature_enable(IMU_FEATURE_ANY_MOTION, true);
    if (result != IMU_SUCCESS)
    {
        return;
    }
}
```

使用 `imu_get_motion_status` 读取 Any/No/Sig-Motion 与 Tap 状态，使用 `imu_get_tap_status` 获得敲击次数、轴和极性。参数的物理含义、阈值单位和可用范围应按 `QMI8658B-Datasheet.pdf` 的相关表格设置，并结合整机结构完成标定。

## 十、偏置、校准和硬件自检

`imu_set_acc_offset` 和 `imu_set_gyr_offset` 的参数不是直接使用 mg 或 mdps，而是芯片规定的 16 位有符号偏置数值。换算方法如下：

| 接口 | 传入值表示的物理量 | 换算方法 | 示例 |
|---|---|---|---|
| `imu_set_acc_offset` | 加速度偏置，单位 g | 传入值 = 偏置值(g) × 4096 | `4096` 表示 `+1 g`，`-205` 约表示 `-0.05 g`（约 `-50 mg`） |
| `imu_set_gyr_offset` | 陀螺仪偏置，单位 dps | 传入值 = 偏置值(dps) × 32 | `32` 表示 `+1 dps`，`-160` 表示 `-5 dps` |

原始资料中的 `signed 4.12` 和 `signed 11.5` 分别表示“小数点后保留 12 位”和“小数点后保留 5 位”的有符号定点数格式。两个接口都会通过内部 CTRL9 命令应用设置；偏置在传感器断电或复位后失效。应用层应保存经过换算和标定得到的参数，不能把采样输出的 mg/mdps 数值直接传入。

`imu_run_calibration` 会执行片内 COD 校准并输出六字节陀螺仪增益。执行时必须保持传感器静止；需要持久化时由业务层保存输出值，并在合适时机使用 `imu_apply_gyro_gain` 恢复。

```c
static void app_imu_restore_gyro_gain(const uint8_t gain[6])
{
    imu_result_t result;

    result = imu_apply_gyro_gain(gain);
    if (result != IMU_SUCCESS)
    {
        return;
    }
}
```

```c
static void app_imu_self_test(void)
{
    imu_self_test_result_t result_data;
    imu_result_t result;

    result = imu_run_self_test(0x03U, &result_data);
    if (result != IMU_SUCCESS)
    {
        return;
    }

    /* result_data.acc_pass 与 result_data.gyr_pass 均为 true 表示自检通过。 */
}
```

自检掩码 `0x01` 选择加速度计，`0x02` 选择陀螺仪，`0x03` 同时选择六轴。自检会临时改写芯片配置，完成后驱动会恢复测试前的配置；业务侧仍应在自检后重新读取一次数据，确认业务状态正常。
