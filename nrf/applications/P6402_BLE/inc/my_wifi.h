/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_wifi.h
**文件描述:        WIFI管理头文件
**当前版本:        V1.0
**作    者:        Felix Tang (tangchaofa@jimiiot.com)
**完成日期:        2026.08.03
*********************************************************************/
#ifndef _MY_WIFI_H_
#define _MY_WIFI_H_


int my_wifi_init(k_tid_t *tid);

int my_wifi_send_msg_data(char *msg);

#endif
