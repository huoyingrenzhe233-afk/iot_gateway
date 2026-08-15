#include "dianji.h"

void dianjiInit(void){
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	GPIO_InitTypeDef GPIO_Struct;
	
	GPIO_Struct.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Struct.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12;
	GPIO_Struct.GPIO_Speed = GPIO_Speed_50MHz;
	
	GPIO_Init(GPIOA,&GPIO_Struct);
	
	GPIO_Struct.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Struct.GPIO_Pin = GPIO_Pin_1;
	GPIO_Init(GPIOA,&GPIO_Struct);
	
	TIM_OCInitTypeDef OC_Struct;
	TIM_OCStructInit(&OC_Struct);
	OC_Struct.TIM_OCMode = TIM_OCMode_PWM1;
	OC_Struct.TIM_OCPolarity = TIM_OCPolarity_High;
	OC_Struct.TIM_OutputState = TIM_OutputState_Enable;
	OC_Struct.TIM_Pulse = 0;
	TIM_OC2Init(TIM2,&OC_Struct);
	
	
}
