/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_comm.h
**文件描述:        LL311_BLE 工程统一头文件
**当前版本:        V1.0
**作    者:        Harrison Wu (wuyujiao@jimiiot.com)
**完成日期:        2026.01.15
*********************************************************************
** 功能描述:        集中引用所有模块头文件，便于统一管控
**                 包含：Main、BLE、Shell、Ctrl、LTE、GSensor 模块
*********************************************************************/

#ifndef _MY_COMMON_H_
#define _MY_COMMON_H_

/* ========== 系统头文件引用 ========== */
/* 标准C库 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <psa/crypto.h>

/* Zephyr核心 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>

/* Zephyr驱动 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/timer/system_timer.h>

/* Zephyr系统功能 */
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_output_custom.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/clock.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/zms.h>

/* Zephyr 设备及电源管理 */
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

/* Zephyr shell */
#include <zephyr/shell/shell.h>

/* Zephyr蓝牙 */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/addr.h>

/* Zephyr MCUmgr */
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>

/* Nordic SDK/HAL */
#include <hal/nrf_gpio.h>
#include <hal/nrf_reset.h>
#include <bluetooth/services/nus.h>
#include <soc.h>
#include <uart_async_adapter.h>

/* ========== 通用宏定义 ========== */
#define JM_SLEEP(timeout) k_sleep(timeout)
#define MY_MALLOC_BUFFER(PTR, BUFFER_SIZE) \
    {                                      \
        (PTR) = k_malloc((BUFFER_SIZE));   \
    }
#define MY_FREE_BUFFER(PTR) k_free(PTR)
#define MY_ASSERT_INFO(PARAM)  \
    {                          \
        if (!(PARAM))          \
        {                      \
            my_system_reset(); \
        }                      \
    } // TODO

/* ========== 模块类型枚举 ========== */
typedef enum
{
    MOD_MAIN,        // 主处理程序
    MOD_BLE,         // BLE处理程序
    MOD_CTRL,        // Control处理程序
    MOD_LTE,         // LTE处理程序
    MOD_WIFI,        // WIFI处理程序
    MOD_GSENSOR,     // G-Sensor处理程序
    MOD_FOTA,        // FOTA处理程序
    MAX_MY_MOD_TYPE, // 最大模块类型
} module_type;

/* ========== 告警类型枚举 ========== */
typedef enum
{
    ALARM_CHARGE_IN = 1,        // 充电器插入告警
    ALARM_CHARGE_OUT,           // 充电器拔出告警
    ALARM_CHARGE_FULL,          // 充满状态告警
    ALARM_BAT_SWITCH,           // 电量状态切换告警
    ALARM_BLE_CONNECTED,        // 蓝牙连接成功告警
    ALARM_BLE_CONNECT_ERR,      // 蓝牙连接异常告警
    ALARM_LOW_BAT,              // 内置电池低电报警
    ALARM_OTHER,                // 其他类型告警
} alarm_type_t;

/* ========== 4G开机/唤醒协议相关枚举 ========== */
typedef enum
{
    LTE_PWR_STATE_NORMAL = 0,   // 正常上电
    LTE_PWR_STATE_ABNORMAL = 1, // 异常重启
} lte_power_state_t;

typedef enum
{
    LTE_PWRON_REASON_BT = 0,    // 蓝牙上电
    LTE_PWRON_REASON_USB = 1,   // USB上电
    LTE_PWRON_REASON_OTHER = 2, // 其他(预留)
} lte_poweron_reason_t;

typedef enum
{
    LTE_BOOT_REASON_KEYPRESS = 0,   // 按键开机
    LTE_BOOT_REASON_INTERVAL,       // 间隔定位上报
    LTE_BOOT_REASON_ALARM,          // 告警事件唤醒
    LTE_BOOT_REASON_SCAN,           // 扫描数据上报
    LTE_BOOT_REASON_RESERVED = 255, // 预留(未知原因)
} lte_boot_reason_t;

/* ========== 定时器相关定义 ========== */
typedef enum
{
    MY_TIMER_ONE_MINUTE = 0, // 最核心定时器，一分钟定时器使用
    MY_TIMER_TEST,           // 1
    MY_TIMER_WDT_FEED,       // 看门狗喂狗定时器
    MY_TIMER_LTE_PULSE,       // LTE脉冲定时器
    MY_TIMER_PATM_UPLOAD,     // 气压定时上传定时器

    /* 扫描定时器 */
    MY_TIMER_SCAN_INTERVAL,   // 周期扫描定时器
    MY_TIMER_SCAN_LENGTH,     // 单次扫描时长定时器
    MY_TIMER_UPLOAD_INTERVAL, // 上报间隔定时器
    MY_TIMER_BLUETOOTH_KEY,   // 蓝牙按键定时器
    MY_TIMER_BLUETOOTH_ADV,   // BLE连接广播定时器

    // LED 控制定时器
    MY_TIMER_LED_ENABLE,           // LED使能定时器
    MY_TIMER_LED_BLINK,            // LED闪烁定时器

    MY_TIMER_MAX_ID,
} MY_E_TIMER;

/* 消息ID定义 */
typedef enum
{
    MY_MSG_BASE_MSG = 0,
    MY_MSG_UART_READ_EVENT = MY_MSG_BASE_MSG + 1,
    MY_MSG_TEST,
    MY_MSG_ONE_MINUTE_TIMER,
    MY_MSG_GET_MDIMEI,
    MY_MSG_CLEAR_WDT,
    MY_MSG_POF_EVENT,
    MY_MSG_SYS_SHUTDOWN,
    MY_MSG_POWER_OFF,
    MY_MSG_SYS_REBOOT, // 10
    MY_MSG_BLE_DATA_EVENT,
    MY_MSG_CTRL_LED,    /* LED 控制消息 */
    MY_MSG_CTRL_BUZZER_MODE, /* 蜂鸣器控制消息 */
    MY_MSG_CTRL_BUZZER_ON,
    MY_MSG_CTRL_BUZZER_OFF,
    MY_MSG_SHOW_CHARG, // 充电状态显示LED消息
    MY_MSG_UPDATE_BATTERY, // 更新电池状态消息

    /* LTE处理程序消息 */
    MY_MSG_LTE_PWRON,
    MY_MSG_LTE_PWROFF,
    MY_MSG_RETRANS_CHECK,
    MY_MSG_ADD_RETRANS_QUEUE,
    MY_MSG_LTE_PULSE_START,
    MY_MSG_LTE_PULSE_STOP,

    /* G-Sensor处理程序消息 */
    MY_MSG_GSENSOR_INT,             /* G-Sensor INT1 中断消息 */
    MY_MSG_READ_GSENSOR_DATA,       /* 读取G-Sensor数据消息 */

    /* UART消息 */
    MY_MSG_UART_TX_DONE,
    MY_MSG_UART_TX_ABORTED,
    MY_MSG_UART_SEND,
    MY_MSG_UART_REV,
    MY_MSG_UART_IDLE,

    /* CTRL处理程序消息 */
    MY_MSG_CTRL_KEY_SHORT_PRESS,       /* 按键短按事件 */
    MY_MSG_CTRL_KEY_LONG_PRESS,        /* 按键长按事件（3秒） */
    MY_MSG_CTRL_SHUTDOWN_REQUEST,      /* 关机请求 */
    MY_MSG_CTRL_PATM_TIMER,            /* 气压定时上传触发消息 */
    MY_MSG_CTRL_PATM_RELOAD,           /* 气压定时器配置更新消息 */
    MY_MSG_CTRL_PATM_READ,             /* 读取气压数据消息 */
    MY_MSG_CTRL_STATUS_READ,           /* 读取状态信息消息 */
    MY_MSG_LED_CTRL_MODE,              /* LED 控制 */
    MY_MSG_LED_ENABLE,                 /* LED使能 */
    MY_MSG_LED_DISABLE,                /* LED禁用 */

    /* BLE 处理程序消息 */
    MY_MSG_BLE_RX,
    MY_MSG_BLE_TX,                  /* 向蓝牙发送消息 */
    MY_MSG_BLE_OPEN_ADV,            /* 开启可连接广播 */
    MY_MSG_BLE_CLOSE_ADV,           /* 关闭可连接广播 */
    MY_MSG_BLE_SENSOR_BP_SAMPLE,    /* 气压采样结果消息 */
    MY_MSG_BLE_SENSOR_LTE_ACK,      /* LTE异步应答转发给BLE */

    // 处理4G过来LTE+CMD数据透传
    MY_MSG_LTE_CMD_RX,
    MY_MSG_LTE_CMD_ASYNC_RESP,

    //处理透传mac和tag数据传输（单条上报)
    MY_MSG_UPLOAD_TAG_AND_MAC,

    /* DFU OTA 状态消息 */
    MY_MSG_DFU_START,    /* DFU OTA 开始 */
    MY_MSG_DFU_TIMEOUT,  /* DFU OTA 超时退出 */
    MY_MSG_DFU_COMPLETE, /* DFU OTA 完成 */
    MY_MSG_DFU_FAIL,     /* DFU OTA 失败 */

    MY_MSG_LTE_BLE_DATA,        /* 蓝牙指令数据 */
    MY_MSG_BLE_PACKET_TIMEOUT,  /* BLE包传输应答超时 */
    MY_MSG_LTE_PULSE,             /* LTE脉冲消息 */

    /* 扫描处理程序消息 */
    MY_MSG_TAG_SCAN_PROCESS,    /* TAG扫描数据处理消息 */
    MY_MSG_TRAN_MAC_PROCESS,    /* 透传MAC扫描数据处理消息 */
    MY_MSG_SCAN_INTERVAL,       /* 周期扫描定时器消息 */
    MY_MSG_SCAN_LENGTH,         /* 单次扫描时长定时器消息 */
    MY_MSG_SCAN_UPLOAD,         /* 上报间隔定时器消息 */
    MY_MSG_UPLOAD_WAKEUP,       /* LTE就绪后触发BLE统一调度扫描与传感器缓存上报 */
    MY_MSG_LTE_WAKEUP,          /* LTE唤醒引脚中断触发的UART恢复消息 */
} MY_MAIN_TASK_MSG;

/* ========== 集中引用所有模块头文件 ========== */
#include "my_version.h"
#include "my_ring_buf.h"
#include "my_main.h"
#include "my_ble_core.h"
#include "my_shell.h"
#include "my_lte.h"
#include "my_gsensor.h"
#include "my_battery.h"
#include "my_wdt.h"
#include "my_tool.h"
#include "my_ble_app.h"
#include "my_cmd_setting.h"
#include "my_dfu_jimi.h"
#include "my_ble_log.h"
#include "my_pm.h"
#include "my_ble_scan.h"
#include "barometer_api.h"
#include "temp_humi_api.h"
#include "my_flash_store.h"
#include "my_ctrl.h"
#include "my_zms_param.h"
#include "my_uart.h"
#include "my_wifi.h"

#endif /* _MY_COMMON_H_ */
