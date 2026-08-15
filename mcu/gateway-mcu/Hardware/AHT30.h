#ifndef __AHT30_H
#define __AHT30_H
#include "stm32f10x.h"                  // Device header
typedef struct AHT{
	uint8_t status;
	uint32_t tem;
	uint32_t shidu;
	uint8_t crc;
} AHT;


void AHT30Init(void);
void AHT30_WriteData(void);
void getAHT_Data(AHT *data);

#endif
