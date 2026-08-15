#include "stm32f10x.h"                  // Device header
#include "Key.h"
#include "FreeRTOS.h"
#include "task.h"

//PB12和PB14接了两个按键，按键输入设备，所以使用上拉输入
void Key_Init(void)
{
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
	
		GPIO_InitTypeDef GPIO_InitStruct;
		GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;//设置成上拉模式，默认就是1，按键按下就变成0了
		GPIO_InitStruct.GPIO_Pin =  GPIO_Pin_12 | GPIO_Pin_14;
		GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
		GPIO_Init(GPIOB,&GPIO_InitStruct);

}

//如果用户按了B12，返回1，如果用户按了B14，返回2
uint8_t getKeyNum(void)
{
	uint8_t keyNum = 0;
	if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_12)==0)//读取B1引脚的电平
	{
		vTaskDelay(20);//第一次按下
		while(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_12)==0);//按下的时间
		vTaskDelay(20);//第一次松开
		keyNum = 1;
	}
	
	if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_14)==0)
	{
		vTaskDelay(20);//第一次按下
		while(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_14)==0);
		vTaskDelay(20);//第一次按下
		keyNum = 2;
	}
	
	return keyNum;
}
