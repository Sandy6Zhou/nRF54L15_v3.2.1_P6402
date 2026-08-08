/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_gprs.c
**文件描述:        GPRS api implements
**当前版本:        V1.0
**作    者:        Felix Tang (tangchaofa@jimiiot.com)
**完成日期:        2026.08.05
*********************************************************************/
/* 必须在包含 my_comm.h 之前定义 BLE_LOG_MODULE_ID，避免与 my_ble_log.h 中的默认定义冲突 */
#define BLE_LOG_MODULE_ID BLE_LOG_MOD_WIFI

#include "my_comm.h"

LOG_MODULE_REGISTER(my_gprs, LOG_LEVEL_INF);

K_MSGQ_DEFINE(my_gprs_msgq, sizeof(msg_t), 10, 4);
K_THREAD_STACK_DEFINE(my_gprs_task_stack, MY_GPRS_TASK_STACK_SIZE);
static struct k_thread s_my_gprs_task_data;
static MY_SOCADDR_STRUCT s_server_ip_addr = { 0 };    // 服务器IP地址信息



static void my_gprs_task(void *p1, void *p2, void *p3)
{
    msg_t msg;
    char fw_info[512];

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    MY_LOG_INF("GPRS thread started");

    for (;;)
    {
        my_recv_msg(&my_gprs_msgq, (void *)&msg, sizeof(msg_t), K_FOREVER);

        switch (msg.msgID)
        {
            case MY_MSG_WIFI_INIT:
                my_wifi_at_init_sequence();
                break;

            case MY_MSG_WIFI_VERSION:
                my_wifi_get_fw_info(fw_info, sizeof(fw_info));
                break;

            case MY_MSG_WIFI_CONNECT:
                my_wifi_connect_ap(g_wifiSsid, g_wifiPasswd);
                break;

            case MY_MSG_WIFI_GET_IP: // 获取分配的 IP
                break;

            case MY_MSG_SOCKET_GET_HOSTNAME: //域名解析
                // TODO: 
                break;

            case MY_MSG_SOCKET_CONNECT:
                strcpy(s_server_ip_addr.addrStr, "120.77.9.180"); // 服务器域名或IP地址
                s_server_ip_addr.port = 8021; // 服务器端口
                my_wifi_tcp_connect(s_server_ip_addr.addrStr, s_server_ip_addr.port);
                break;

            case MY_MSG_SOCKET_SEND:
                my_wifi_tcp_send((uint8_t *)"hello server", 12);
                break;

            case MY_MSG_SOCKET_REV:
                break;

            case MY_MSG_SOCKET_CLOSE:
                my_wifi_tcp_close();
                break;

            default:
                break;
        }
    }
}

/********************************************************************
**函数名称:  my_gprs_init
**入口参数:  tid      ---        指向线程 ID 变量的指针
**出口参数:  tid      ---        存储启动后的线程 ID
**函数功能:  初始化GPRS模块并启动线程
**返回值:    0 表示成功，其他表示失败
*********************************************************************/
int my_gprs_init(k_tid_t *tid)
{
    int err;

    // 初始化消息队列
    my_init_msg_handler(MOD_GPRS, &my_gprs_msgq);

    *tid = k_thread_create(&s_my_gprs_task_data, my_gprs_task_stack,
                        K_THREAD_STACK_SIZEOF(my_gprs_task_stack),
                        my_gprs_task, NULL, NULL, NULL,
                        MY_GPRS_TASK_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(*tid, "MY_GPRS");

    // 提前注册TCP数据接收回调
    my_wifi_register_tcp_recv_cb(my_tcp_data_handler);

    LOG_INF("GPRS module initialized");
    return 0;
}

void my_tcp_data_handler(uint8_t *data, uint16_t len)
{
    MY_LOG_INF("Received TCP data: %.*s", len, data);
}
