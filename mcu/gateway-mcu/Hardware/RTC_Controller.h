#ifndef __RTC_CONTROLLER_H
#define __RTC_CONTROLLER_H

#include "stm32f10x.h"                  // Device header

void RTC_CTRL_Init(void);


void setAlarm(uint32_t alarmValue);

void RTC_InterruptInit(void);

void setCounter(int32_t counter);

/* 将Unix时间戳（秒）转换为 "YYYY-MM-DD HH:MM:SS" 字符串，tzHour为时区（北京=8） */
void UnixTimeToStr(uint32_t unixtime, int8_t tzHour, char *out);
#endif
