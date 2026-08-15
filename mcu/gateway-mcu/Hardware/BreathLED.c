#include "BreathLED.h"
#include "Delay.h"


void BreathLEDInit(void){
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);
	GPIO_InitTypeDef GPIO_Struct;
	GPIO_Struct.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Struct.GPIO_Pin = GPIO_Pin_0;
	GPIO_Struct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_Struct);
	
	TIM_InternalClockConfig(TIM2);
	TIM_TimeBaseInitTypeDef TimeBase_Struct;
	TimeBase_Struct.TIM_ClockDivision = TIM_CKD_DIV1;
	TimeBase_Struct.TIM_CounterMode = TIM_CounterMode_Up;
	TimeBase_Struct.TIM_Period = 10000-1;
	TimeBase_Struct.TIM_Prescaler = 144-1;
	TimeBase_Struct.TIM_RepetitionCounter = 0x00;
	TIM_TimeBaseInit(TIM2,&TimeBase_Struct);
	TIM_ClearFlag(TIM2,TIM_FLAG_Update);
	
	
	TIM_OCInitTypeDef OC_Struct;
	TIM_OCStructInit(&OC_Struct);
	
	OC_Struct.TIM_OCMode = TIM_OCMode_PWM1;
	OC_Struct.TIM_OCPolarity = TIM_OCPolarity_High;
	OC_Struct.TIM_OutputState = TIM_OutputState_Enable;
	OC_Struct.TIM_Pulse = 0;
	
	TIM_OC1Init(TIM2,&OC_Struct);
	
	/* 注：不再配置 PA2/TIM2 CH3 —— PA2 是 ZigBee 模块的 USART2 TX（见 Zigbee_Init），
	 * 把它复用成 TIM2 CH3 会压住 TX 线，导致 MCU 向 ZigBee 发送的数据损坏。
	 * 呼吸灯只用 TIM2 CH1（PA0），与 CH3 无关。 */
	
	//TIM_ITConfig(TIM3,TIM_IT_Update,ENABLE);
	
	//TIM使能
	TIM_Cmd(TIM2,ENABLE);
}

