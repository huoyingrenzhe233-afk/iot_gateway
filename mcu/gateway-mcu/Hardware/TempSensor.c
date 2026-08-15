#include "TempSensor.h"

void TempSensorInit(void)
{
	GPIO_InitTypeDef GPIO_Struct;
	GPIO_Struct.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Struct.GPIO_Pin = GPIO_Pin_4;
	GPIO_Struct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_Struct);
}
