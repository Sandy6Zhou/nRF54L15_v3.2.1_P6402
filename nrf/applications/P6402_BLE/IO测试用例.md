# P6402_BLE IO 口功能测试用例

## 一、测试准备

1. **编译开关**：`src/my_shell.c` 中 `#define IO_TEST_ENABLE 1`。测试完成后将该宏置 0 或直接删除整个 `IO_TEST_ENABLE` 区块（`#if IO_TEST_ENABLE ... #endif`）即可移除全部测试命令。
2. **启用方法**：编译烧录后，通过 RTT 进入 shell，命令前缀为 `app iot <子命令>`。
3. **测试辅助**：LCD 清屏颜色为 RGB565（红 `F800`、绿 `07E0`、蓝 `001F`、白 `FFFF`、黑 `0000`）。

## 二、命令总览

| 子命令 | 功能 | 关联引脚/外设 |
|---|---|---|
| `iot pwr <target> <on\|off>` | 模块电源控制 | 4G P2.06 / 充电 P2.07 / WIFI P2.08 / 气压计 P2.09 / 六轴 P2.10 / LCD P2.00 |
| `iot i2cscan <21\|22>` | I2C 总线扫描 | i2c21(P1.03/1.04)、i2c22(P1.08/1.07) |
| `iot gsensor` | 读六轴数据 | QMI8658B（i2c21） |
| `iot gauge` | 读库仑计电量 | OM70201WV（i2c21） |
| `iot baro` | 读气压计 | SPA06（i2c22） |
| `iot charge` | 读充电检测 | P0.03 |
| `iot key` | 读按键电平 | 功能键 P1.09 / SOS 键 P1.14 |
| `iot lcd <on\|off>` | LCD 电源开关 | P2.00 |
| `iot lcdbl <0-100>` | LCD 背光调节 | pwm21 P1.11 |
| `iot lcdclear <hex16>` | LCD 清屏 | ST7735（spi00） |
| `iot buzzer` | 蜂鸣器测试音 | pwm20 P1.12 |
| `iot batt` | 读电池 ADC 电压 | AIN6 P1.13 |
| `iot ltetx <hexstr>` | LTE 串口发送 | uart30 P0.00/P0.01 |
| `iot wifitx <hexstr>` | WIFI 串口发送 | uart20 P1.05/P1.06 |
| `iot wake <lte\|wifi>` | 读唤醒引脚电平 | LTE P0.04 / WIFI P0.02 |
| `iot all` | 一键执行核心自检 | 自动依次执行 |

## 三、用例详情

### 3.1 电源控制 GPIO（`iot pwr`）

| 目标 | 引脚 | 有效电平 | 测试命令 |
|---|---|---|---|
| 4G | P2.06 | 高电平有效 | `app iot pwr 4g on` |
| 充电使能 | P2.07 | 低电平使能 | `app iot pwr charge on` |
| WIFI | P2.08 | 高电平有效 | `app iot pwr wifi on` |
| 气压计 | P2.09 | 高电平有效 | `app iot pwr baro on` |
| 六轴 | P2.10 | 低电平有效 | `app iot pwr gsensor on` |
| LCD | P2.00 | 高电平有效 | `app iot pwr lcd on` |

- **操作步骤**：
  1. 分别对每个目标执行 `on` 与 `off`。
  2. 用万用表测量对应引脚电平是否符合上表"有效电平"（on 时有效、off 时无效）。
- **预期结果**：
  - 命令执行成功：`iot pwr gsensor = ON OK` / `iot pwr gsensor = OFF OK`
  - 引脚电平与有效电平一致（on 时拉有效、off 时拉无效）。
  - 参数错误时打印：`iot pwr <4g|charge|wifi|baro|gsensor|lcd> <on|off>` 或 `Invalid target: xxx`。

### 3.2 I2C 总线扫描（`iot i2cscan`）

- **操作步骤**：执行 `app iot i2cscan 21` 和 `app iot i2cscan 22`。
- **预期结果**：
  - i2c21（六轴+库仑计）应扫描到：
    ```
    I2C scan on bus 21 ...
      found device at 0x38    (OM70201WV 库仑计)
      found device at 0x6B    (QMI8658B 六轴)
    Scan done, 2 device(s) found
    ```
  - i2c22（气压计）应扫描到：
    ```
    I2C scan on bus 22 ...
      found device at 0x77    (SPA06 气压计)
    Scan done, 1 device(s) found
    ```
    > 注：若总线上还挂有其他从机，会一并列出其地址。
  - 总线未就绪：`I2C bus 21 not ready`。

### 3.3 六轴传感器（`iot gsensor`）

- **操作步骤**：执行 `app iot gsensor`，缓慢翻转/晃动板卡。
- **预期结果**：
  ```
  ACC(mg): 12,8,995  GYR(mdps): 45,-12,120
  ```
  - 静止时 Z 轴约 ±1000mg（重力），XY 接近 0；翻转后三轴数值随姿态变化。
  - 通信失败：`QMI8658B read fail: -5`。

### 3.4 库仑计（`iot gauge`）

- **操作步骤**：执行 `app iot gauge`。
- **预期结果**：
  ```
  Vbat=4100mV Ibat=-320mA Tbat=26C SOC=87% SOH=100%
  ```
  - 电压随电池状态变化；充电时 Ibat 为正，放电为负；SOC 0~100。
  - 通信失败：`OM70201WV read fail: 1`。

### 3.5 气压计（`iot baro`）

- **操作步骤**：执行 `app iot baro`，可对气压计气孔吹气观察变化。
- **预期结果**：
  ```
  Pressure=101325Pa Temp=25.60C
  ```
  - 海平面约 101325Pa，吹气时压力上升。
  - 通信失败：`SPA06 read fail: 1`。

### 3.6 充电检测（`iot charge`）

- **操作步骤**：执行 `app iot charge`。先不插充电器测一次，再插入充电器测一次。
- **预期结果**：
  - 未插充电器：`Charge detect level = 0 (NO_CHARGE)`
  - 插入充电器：`Charge detect level = 1 (CHARGING)`

### 3.7 按键电平（`iot key`）

- **操作步骤**：执行 `app iot key`。先不按键测一次，再分别按下功能键、SOS 键测。
- **预期结果**：
  - 未按键：`FUN_KEY(P1.09)=0  SOS_KEY(P1.14)=0`
  - 按下功能键：`FUN_KEY(P1.09)=1  SOS_KEY(P1.14)=0`
  - 按下 SOS 键：`FUN_KEY(P1.09)=0  SOS_KEY(P1.14)=1`
- **补充验证**：SOS 键短按/长按，RTT 应打印：
  ```
  SOS KEY EVENT: Short press detected
  SOS KEY EVENT: Long press detected (3s)
  ```

### 3.8 LCD 电源（`iot lcd`）

- **操作步骤**：执行 `app iot lcd on`，观察屏幕背光/显示是否点亮；再执行 `app iot lcd off` 观察熄灭。
- **预期结果**：
  ```
  LCD power = ON OK
  LCD power = OFF OK
  ```
  - 屏幕供电引脚 P2.00 拉高时屏幕工作，拉低时断电。

### 3.9 LCD 背光（`iot lcdbl`）

- **操作步骤**：执行 `app iot lcdbl 0`、`app iot lcdbl 50`、`app iot lcdbl 100`，逐档观察亮度变化。
- **预期结果**：
  ```
  LCD backlight = 0% OK
  LCD backlight = 50% OK
  LCD backlight = 100% OK
  ```
  - 亮度随数值增大而变亮，0 时背光熄灭。

### 3.10 LCD 清屏（`iot lcdclear`）

- **操作步骤**：依次执行：
  ```
  app iot lcdclear F800    (红色)
  app iot lcdclear 07E0    (绿色)
  app iot lcdclear 001F    (蓝色)
  app iot lcdclear FFFF    (白色)
  ```
- **预期结果**：
  ```
  LCD clear 0xF800 OK
  ```
  - 屏幕整屏分别显示红/绿/蓝/白。
  - 若颜色与命令不符（如红绿颠倒），在 overlay 的 st7735r 节点打开 `rgb-is-inverted` 或调整 `madctl` 后重测。

### 3.11 蜂鸣器（`iot buzzer`）

- **操作步骤**：执行 `app iot buzzer`，贴近蜂鸣器听音。
- **预期结果**：
  ```
  Buzzer 2KHz 300ms played
  ```
  - 蜂鸣器发出约 300ms 的 2KHz 提示音。

### 3.12 电池 ADC（`iot batt`）

- **操作步骤**：执行 `app iot batt`。
- **预期结果**：
  ```
  Battery voltage = 4100mV
  ```
  - 电压为当前电池电压（范围约 3000~4200mV）。
  - 采样失败：`Battery ADC read fail: -5`。

### 3.13 LTE 串口发送（`iot ltetx`）

- **操作步骤**：用串口工具连接 LTE 模块（或抓取 uart30 TX 波形），执行：
  ```
  app iot ltetx "AA 0D 0A"
  ```
- **预期结果**：
  ```
  LTE TX 3 bytes OK
  ```
  - uart30 的 TX（P0.00）输出字节 `AA 0D 0A`。
  - 参数错误：`Usage: iot ltetx <hexstr>  e.g. "AA 0D 0A"`。

### 3.14 WIFI 串口发送（`iot wifitx`）

- **操作步骤**：WIFI 串口（uart20）默认注册 PING/PONG 测试回调。发送 "PING" 的十六进制：
  ```
  app iot wifitx "50 49 4E 47"
  ```
- **预期结果**：
  ```
  WIFI TX 4 bytes OK
  ```
  - RTT 日志随后打印收到回显：
    ```
    wifi_uart_rx_test  <十六进制数据 50 49 4E 47>
    WIFI UART test rx matched PING, reply PONG
    ```
  - WIFI 串口 RX（P1.06）应能收到回发的 `PONG\r\n`。

### 3.15 唤醒引脚（`iot wake`）

- **操作步骤**：执行 `app iot wake lte` 和 `app iot wake wifi`。正常时外部上拉为高电平。
- **预期结果**：
  ```
  lte wake pin level = 1
  wifi wake pin level = 1
  ```
  - 若被对端模块拉低唤醒，则读到 0。

### 3.16 一键自检（`iot all`）

- **操作步骤**：执行 `app iot all`。
- **预期结果**：依次打印各外设测试结果：
  ```
  === IO test start ===
  Charge detect level = 0 (NO_CHARGE)
  FUN_KEY(P1.09)=0  SOS_KEY(P1.14)=0
  Battery voltage = 4100mV
  ACC(mg): 12,8,995  GYR(mdps): 45,-12,120
  Vbat=4100mV Ibat=-320mA Tbat=26C SOC=87% SOH=100%
  Pressure=101325Pa Temp=25.60C
  Buzzer 2KHz 300ms played
  === IO test done ===
  ```
  - 所有行正常输出即核心 IO 链路通畅；出现某行 `fail` 则定位对应外设。

## 四、测试记录模板

| 用例 | 命令 | 实测结果 | 预期结果 | 通过 |
|---|---|---|---|---|
| 电源控制-六轴 | `app iot pwr gsensor on` | | `iot pwr gsensor = ON OK` | ☐ |
| ... | | | | |
