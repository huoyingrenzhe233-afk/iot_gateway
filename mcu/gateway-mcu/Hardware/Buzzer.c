#include "Buzzer.h"

void BuzzerInit(void)
{
	GPIO_InitTypeDef GPIO_Struct;
	GPIO_Struct.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Struct.GPIO_Pin = GPIO_Pin_7;
	GPIO_Struct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_Struct);
	
	GPIO_SetBits(GPIOA,GPIO_Pin_7);
	
}
