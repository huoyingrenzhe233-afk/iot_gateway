#include "DMA.h"

void DMACopy(uint16_t *data)
{
	
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1,ENABLE);
	DMA_InitTypeDef DMA_Struct;

	//内存方
	DMA_Struct.DMA_MemoryBaseAddr = (uint32_t)data;
	DMA_Struct.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
	DMA_Struct.DMA_MemoryInc = DMA_MemoryInc_Enable;
	
	//外设方
	DMA_Struct.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
	DMA_Struct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
	DMA_Struct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;//外设不自增,因为只有一个寄存器
	//方向（外设是源还是目标）
	DMA_Struct.DMA_DIR = DMA_DIR_PeripheralSRC;
	//缓冲大小（几个数）
	DMA_Struct.DMA_BufferSize = 2;
	
	//需求
	DMA_Struct.DMA_M2M = DMA_M2M_Disable;
	DMA_Struct.DMA_Mode = DMA_Mode_Circular;//循环
	DMA_Struct.DMA_Priority = DMA_Priority_Medium;//优先级
	
	DMA_Init(DMA1_Channel1,&DMA_Struct);
	DMA_Cmd(DMA1_Channel1,ENABLE);
}
