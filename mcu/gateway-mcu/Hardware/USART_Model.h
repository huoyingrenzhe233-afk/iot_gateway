#ifndef __USART_MODEL_H
#define __USART_MODEL_H
#include "stm32f10x.h"                  // Device header

#define BUFFER_SIZE 300

extern char message[BUFFER_SIZE];
extern uint8_t flag;

void USART1_Init(void);
void Send_Byte(uint8_t data);
void Send_Arr(uint8_t *arr , uint16_t len);
void Send_String(char *arr);

void processNewData(void);
#endif
