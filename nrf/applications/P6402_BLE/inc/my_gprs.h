/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_gprs.h
**文件描述:        GPRS头文件
**当前版本:        V1.0
**作    者:        Felix Tang (tangchaofa@jimiiot.com)
**完成日期:        2026.08.05
*********************************************************************/
#ifndef _MY_GPRS_H_
#define _MY_GPRS_H_

#define MAX_DOMAIN_LEN       64 // 域名最大长度

typedef struct
{
    char addrStr[MAX_DOMAIN_LEN];
    uint16_t port;
    uint8_t isIpAddr;
} MY_SOCADDR_STRUCT;

int my_gprs_init(k_tid_t *tid);

void my_tcp_data_handler(uint8_t *data, uint16_t len);

#endif
