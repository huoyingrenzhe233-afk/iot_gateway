#include "stm32f10x.h"                  // Device header

//LED1---PA0     LED2--PA1
void LED_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStruct.GPIO_Pin =GPIO_Pin_2 ;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStruct);

	//初始化的时候，灯都是灭的
	GPIO_ResetBits(GPIOA,GPIO_Pin_2);
}
//点亮LED1
void LED1_ON(void)
{
	GPIO_SetBits(GPIOA,GPIO_Pin_2 );
}

void LED1_OFF(void)
{
	GPIO_ResetBits(GPIOA,GPIO_Pin_2 );
}

void LED1_Toggle(void)
{
	//读取A0引脚当前的状态
	 if(GPIO_ReadOutputDataBit(GPIOA,GPIO_Pin_2))
	 {
		LED1_OFF();
	 }
	 else
	 {
		LED1_ON();
	 }
}
	
void LED2_ON(void)
{
	GPIO_SetBits(GPIOA,GPIO_Pin_1 );
}

void LED2_OFF(void)
{
	GPIO_ResetBits(GPIOA,GPIO_Pin_1 );
}

void LED2_Toggle(void)//翻转电平
{
	//读取A0引脚当前的状态
	 if(GPIO_ReadOutputDataBit(GPIOA,GPIO_Pin_1))
	 {
		LED2_OFF();
	 }
	 else
	 {
		LED2_ON();
	 }
}


void LED_PWM(void)
{
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3,ENABLE);
	GPIO_InitTypeDef GPIO_Struct;
	GPIO_Struct.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Struct.GPIO_Pin = GPIO_Pin_6;
	GPIO_Struct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_Struct);
	
	TIM_InternalClockConfig(TIM3);
	TIM_TimeBaseInitTypeDef TimeBase_Struct;
	TimeBase_Struct.TIM_ClockDivision = TIM_CKD_DIV1;
	TimeBase_Struct.TIM_CounterMode = TIM_CounterMode_Up;
	TimeBase_Struct.TIM_Period = 10000-1;
	TimeBase_Struct.TIM_Prescaler = 144-1;
	TimeBase_Struct.TIM_RepetitionCounter = 0x00;
	TIM_TimeBaseInit(TIM3,&TimeBase_Struct);
	TIM_ClearFlag(TIM3,TIM_FLAG_Update);
	
	
	TIM_OCInitTypeDef OC_Struct;
	TIM_OCStructInit(&OC_Struct);
	
	OC_Struct.TIM_OCMode = TIM_OCMode_PWM1;
	OC_Struct.TIM_OCPolarity = TIM_OCPolarity_High;
	OC_Struct.TIM_OutputState = TIM_OutputState_Enable;
	OC_Struct.TIM_Pulse = 0;
	
	TIM_OC1Init(TIM3,&OC_Struct);
	
	
	//TIM_ITConfig(TIM3,TIM_IT_Update,ENABLE);
	
	//TIM使能
	TIM_Cmd(TIM3,ENABLE);


}
