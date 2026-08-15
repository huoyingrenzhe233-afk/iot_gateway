#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <USART_Model.h>

//接收数据的容器
char message[BUFFER_SIZE];
uint8_t flag = 0;//0 没有新的数据 1有新的数据

//DMA环形缓冲
char USART_DMABuffer[BUFFER_SIZE];

uint16_t dmaPosition = 0;
uint16_t lastPosition = 0;//上一次数据读到的位置

void USART_RxDMA_Config(void){
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1,ENABLE);
	
	DMA_InitTypeDef DMA_Struct;
	DMA_Struct.DMA_PeripheralBaseAddr = (uint32_t) &USART1->DR;
	DMA_Struct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_Struct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;

	DMA_Struct.DMA_MemoryBaseAddr = (uint32_t)USART_DMABuffer;
	DMA_Struct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	DMA_Struct.DMA_MemoryInc = DMA_MemoryInc_Enable;

	DMA_Struct.DMA_BufferSize = BUFFER_SIZE;
	DMA_Struct.DMA_DIR = DMA_DIR_PeripheralSRC;
	DMA_Struct.DMA_M2M = DMA_M2M_Disable;
	DMA_Struct.DMA_Mode = DMA_Mode_Circular;//一直接收，计数器为0时，自动恢复到200，继续接收
	DMA_Struct.DMA_Priority = DMA_Priority_Medium;
	DMA_Init(DMA1_Channel5,&DMA_Struct);

	DMA_Cmd(DMA1_Channel5,ENABLE);
}

void USART1_Init(void)
{
	//1.RCC
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	
	//2.GPIO
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;//复用推挽
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9 ;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStruct);
	
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;//浮空或者上拉都可以
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10 ;
	GPIO_Init(GPIOA,&GPIO_InitStruct);
	
	//3.USART 波特率9600 8位数据位 1停止位 无校验
	USART_InitTypeDef USART_InitStruct;
	USART_InitStruct.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
	USART_InitStruct.USART_BaudRate = 115200;
	USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStruct.USART_Parity = USART_Parity_No;
	USART_InitStruct.USART_StopBits = USART_StopBits_1;
	USART_InitStruct.USART_WordLength = USART_WordLength_8b;
	USART_Init(USART1,&USART_InitStruct);
	
		
	//4.IDLE中断
	USART_ITConfig(USART1,USART_IT_IDLE,ENABLE);
	
	//让DMA的配置生效
	USART_RxDMA_Config();
	
	//关联串口和DMA
	USART_DMACmd(USART1,USART_DMAReq_Rx,ENABLE);
	
	//5.NVIC
	NVIC_InitTypeDef NVIC_InitStruct;
	NVIC_InitStruct.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStruct.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStruct);
	
	//6.使能
	USART_Cmd(USART1,ENABLE);
}

void Send_Byte(uint8_t data)
{
	USART_SendData(USART1,data);
    while(USART_GetFlagStatus(USART1,USART_FLAG_TXE) == RESET);
}

void Send_Arr(uint8_t *arr , uint16_t len)
{
	for(uint8_t i = 0 ; i< len; i++)
    {
    	Send_Byte(arr[i]);
    }
}

void Send_String(char *arr)
{
	while(*arr)
	{
		Send_Byte(*arr++);
	}
}

int fputc(int ch , FILE *f)
{
	Send_Byte(ch);
	return ch;
}

//处理接收到的新数据
//环形缓冲区数据搬运函数
void processNewData(void){
	
	//1.找到DMA接收到的数据的当前位置
	//假设是第一次运行，接收到的数据是LEDON,index = 1024 - 195(计数器的值)
	int dmaPosition = BUFFER_SIZE - DMA_GetCurrDataCounter(DMA1_Channel5) ;
	
	int index = 0;
	
	//2.从上一次的位置，拷贝到DMA接收到的数据的位置
	while(lastPosition != dmaPosition && index < BUFFER_SIZE - 1){
		
		message [index++] = USART_DMABuffer[lastPosition++];
		
		if(lastPosition >= BUFFER_SIZE){
			lastPosition = 0;
		}
	}
	message[index] = '\0';
	flag = 1;
}

//LEDON\r\n      LEDOFF\r\n
void USART1_IRQHandler(void)
{
	//进入空闲，意味着DMA数据接收完成了
	if(USART_GetITStatus(USART1,USART_IT_IDLE) == SET)
	{
		
		//先把空闲标志位清除
		(void)USART1->SR;
		(void)USART1->DR;
		
		//进入空闲，说明有新的数据来了，把心数据从DMA缓冲拷贝到message
		processNewData();
	}
}



