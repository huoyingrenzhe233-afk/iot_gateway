#include "ADC.h"
#include "DMA.h"

uint16_t DATA[4] = {0};
void ADCInit(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1,ENABLE); 
	GPIO_InitTypeDef GPIO_Struct;
	GPIO_Struct.GPIO_Pin = GPIO_Pin_4 |GPIO_Pin_5 ;
	GPIO_Struct.GPIO_Mode = GPIO_Mode_AIN;
	GPIO_Struct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_Struct);
	RCC_ADCCLKConfig(RCC_PCLK2_Div6);

	
	//ADC结构体
	ADC_InitTypeDef ADC_Struct;
	//通道数
	ADC_Struct.ADC_NbrOfChannel = 2;
	//工作模式
	ADC_Struct.ADC_Mode = ADC_Mode_Independent;
	//转换方式
	ADC_Struct.ADC_ContinuousConvMode = ENABLE;
	ADC_Struct.ADC_ScanConvMode = ENABLE;
	//触发方式
	ADC_Struct.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
	//数据格式
	ADC_Struct.ADC_DataAlign = ADC_DataAlign_Right;
	ADC_Init(ADC1,&ADC_Struct);
	
	
	ADC_RegularChannelConfig(ADC1,ADC_Channel_4,1,ADC_SampleTime_55Cycles5);
	ADC_RegularChannelConfig(ADC1,ADC_Channel_5,2,ADC_SampleTime_55Cycles5);
	//启动DMA
	DMACopy(DATA);
	


	//ADC使能
	ADC_Cmd(ADC1,ENABLE);

	//重置校验
	ADC_ResetCalibration(ADC1);
	//等待重置校验
	while(ADC_GetResetCalibrationStatus(ADC1) == SET);
	//启动校验
	ADC_StartCalibration(ADC1);
	
	//等待启动校验
	while(ADC_GetCalibrationStatus(ADC1) == SET);
	
	//关联ADC和DMA
	ADC_DMACmd(ADC1,ENABLE);
	
	// 连续模式，也需要软件启动第一次
	ADC_SoftwareStartConvCmd(ADC1,ENABLE);	
}



