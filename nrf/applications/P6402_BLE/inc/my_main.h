/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_main.h
**文件描述:        main.c头文件声明
**当前版本:        V1.0
**作    者:        Harrison Wu (wuyujiao@jimiiot.com)
**完成日期:        2026.01.15
*********************************************************************
** 功能描述:        系统主任务处理
*********************************************************************/

#ifndef _MY_MAIN_H_
#define _MY_MAIN_H_

/*直包含必要的头文件，避免循环包含 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* 任务栈大小定义 */
#define MY_MAIN_TASK_STACK_SIZE    4 * 1024 // 先改为4K，未来开发过程中不够再调整
#define MY_BLE_TASK_STACK_SIZE     4 * 1024 // 2K测试空间不够，暂修改为4K
#define MY_CTRL_TASK_STACK_SIZE    2 * 1024 // 先改为2K，未来开发过程中不够再调整
#define MY_LTE_TASK_STACK_SIZE     8 * 1024
#define MY_WIFI_TASK_STACK_SIZE    2 * 1024
#define MY_GSENSOR_TASK_STACK_SIZE 4 * 1024 // 先改为4K，未来开发过程中不够再调整

/* 任务优先级定义 */
#define MY_MAIN_TASK_PRIORITY    7
#define MY_BLE_TASK_PRIORITY     5
#define MY_CTRL_TASK_PRIORITY    5
#define MY_LTE_TASK_PRIORITY     5
#define MY_WIFI_TASK_PRIORITY    5
#define MY_GSENSOR_TASK_PRIORITY 5

/* 定时器回调函数类型定义 */
typedef void (*TIMER_FUN)(void *param);

/* 消息结构体定义 */
typedef struct
{
    uint32_t msgID;
    void *pData;
    uint32_t DataLen;
} msg_t;

/* 上报方式枚举定义 */
typedef enum
{
    REPORT_MODE_NONE = 0,       /* 0-不上报 */
    REPORT_MODE_GPRS,           /* 1-GPRS */
    REPORT_MODE_GPRS_SMS,       /* 2-GPRS+SMS */
    REPORT_MODE_GPRS_SMS_CALL,  /* 3-GPRS+SMS+CALL */
} report_mode_t;

/* 报警模式枚举定义 */
typedef enum
{
    ALARM_NONE = 0,         /* 0-不报警 */
    ALARM_TEMPORARY,    /* 1-报警30s */
    ALARM_CONTINUOUS,   /* 2-持续报警 */
} alarm_mode_t;

/* Empty状态触发方式枚举定义 */
typedef enum
{
    EMPTY_TRIGGER_NONE = 0,         /* 0-不触发 */
    EMPTY_TRIGGER_ONLINE,           /* 1-在线触发 */
    EMPTY_TRIGGER_CHANGE,           /* 2-状态变化触发 */
} empty_trigger_mode_t;

extern bool g_shutdown_request;

/*********************************************************************
**函数名称:  my_system_reset
**入口参数:  无
**出口参数:  无
**函数功能:  系统复位函数
*********************************************************************/
void my_system_reset(void);

/*********************************************************************
**函数名称:  custom_task_info_init
**入口参数:  无
**出口参数:  无
**函数功能:  初始化任务数据信息结构
*********************************************************************/
void custom_task_info_init(void);

/*********************************************************************
**函数名称:  my_init_msg_handler
**入口参数:  mod      --  模块类型
**           msgq     --  消息队列
**出口参数:  无
**函数功能:  初始化模块消息处理函数
*********************************************************************/
void my_init_msg_handler(module_type mod, struct k_msgq *msgq);

/*********************************************************************
**函数名称:  my_send_msg
**入口参数:  src_mod_id   --  发送消息的源模块ID
**           dest_mod_id  --  接收消息的目标模块ID
**           msg          --  消息ID
**出口参数:  无
**函数功能:  向指定模块发送简单消息 (不带附加数据)
*********************************************************************/
void my_send_msg(module_type src_mod_id, module_type dest_mod_id, uint32_t msg);

/*********************************************************************
**函数名称:  my_send_msg_data
**入口参数:  src_mod_id   --  发送消息的源模块ID
**           dest_mod_id  --  接收消息的目标模块ID
**           msg          --  消息结构体指针 (msg_t)
**出口参数:  无
**函数功能:  向指定模块发送包含数据的完整消息结构
*********************************************************************/
void my_send_msg_data(module_type src_mod_id, module_type dest_mod_id, msg_t *msg);

/*********************************************************************
**函数名称:  my_recv_msg
**入口参数:  msg_queue    --  消息队列
**           msg          --  消息结构体指针 (msg_t)
**           msg_size     --  消息结构体大小
**           wait_option  --  等待选项
**出口参数:  无
**函数功能:  从指定消息队列接收消息
*********************************************************************/
int my_recv_msg(void *msg_queue, void *msg, uint32_t msg_size, k_timeout_t wait_option);

/*********************************************************************
**函数名称:  my_start_timer
**入口参数:  timerId    --  定时器ID
**           ms         --  定时器超时时间 (单位: 毫秒)
**           isPeriod   --  是否重复定时
**           timer_fun  --  定时器超时回调函数
**出口参数:  无
**函数功能:  启动指定定时器
*********************************************************************/
int my_start_timer(int timerId, uint32_t ms, bool isPeriod, TIMER_FUN timer_fun);

/*********************************************************************
**函数名称:  my_stop_timer
**入口参数:  timerId    --  定时器ID
**出口参数:  无
**函数功能:  停止指定定时器
*********************************************************************/
void my_stop_timer(int timerId);

/*********************************************************************
**函数名称:  my_time_is_run
**入口参数:  timerId    --  定时器ID
**出口参数:  无
**函数功能:  检查指定定时器是否正在运行
**返 回 值:  剩余时间（单位：毫秒）
*********************************************************************/
uint32_t my_time_is_run(int timerId);

/********************************************************************
**函数名称:  go_to_shutdown
**入口参数:  无
**出口参数:  无
**函数功能:  关机系统
**返 回 值:  无
*********************************************************************/
int go_to_shutdown(void);

#endif /* _MY_MAIN_H_ */
