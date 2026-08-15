#ifndef __PARSECMD_H
#define __PARSECMD_H
#include "stm32f10x.h"                  // Device header
#include "cJSON.h"

typedef struct
{
	uint8_t valid;
    uint8_t transport;   // 1 = body.transport == "zigbee"，需转发给 ZigBee 模块
    uint8_t led_on;
    uint8_t led_br;

    uint8_t motor_on;
    uint8_t motor_sp;
    uint8_t motor_dir;

    uint8_t buzzer;
} Cmd_t;


typedef struct
{
    uint8_t result;       // 1成功，0失败
    uint8_t led_on;
    uint8_t motor_on;
    uint8_t buzzer;
} Result_t;

Cmd_t ParseCmd(char* msg);

/* 从消息中提取完整 JSON（第一个 { 到最后一个 }），返回长度；找不到返回 0 */
int ExtractJson(char *msg, char *out, int maxLen);


#endif