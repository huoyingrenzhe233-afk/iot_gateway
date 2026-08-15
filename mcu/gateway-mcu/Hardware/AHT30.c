#include "AHT30.h"
#include "crc.h"
#include "OLED.h"
#include "FreeRtos.h"
#include "task.h"
#include "Delay.h"

/* 等待I2C事件，带超时。超时返回0，成功返回1。
 * 防止AHT30应答异常时 while(I2C_CheckEvent) 死循环卡死 collectTask，
 * 导致传感器数据永远无法上传 */
static uint8_t I2C_WaitEvent(I2C_TypeDef* I2Cx, uint32_t event, uint32_t timeoutCnt)
{
	uint32_t t = 0;
	while(I2C_CheckEvent(I2Cx, event) != SUCCESS){
		if(++t >= timeoutCnt){
			return 0;
		}
	}
	return 1;
}

void AHT30Init(void)
{
	//使用硬件I2C协议 
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2,ENABLE);
	
	GPIO_InitTypeDef GPIO_Struct;
	GPIO_Struct.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
	GPIO_Struct.GPIO_Mode = GPIO_Mode_AF_OD;
	GPIO_Struct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_Struct);
	
	I2C_InitTypeDef I2C_Struct;
	I2C_Struct.I2C_Ack = I2C_Ack_Enable;
	I2C_Struct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
	I2C_Struct.I2C_ClockSpeed = 100000;//比较稳
	I2C_Struct.I2C_DutyCycle = I2C_DutyCycle_16_9;//其实就是默认的，对100k不起作用
	I2C_Struct.I2C_Mode = I2C_Mode_I2C;
	I2C_Struct.I2C_OwnAddress1 = 0x00;
	I2C_Init(I2C2,&I2C_Struct);
	I2C_Cmd(I2C2,ENABLE);
	//注意：AHT30Init 在 main() 里、vTaskStartScheduler() 之前调用，
	//调度器未启动时不能用 vTaskDelay（FreeRTOS API），改用 SysTick 延时
	Delay_ms(10);
}

void AHT30_WriteData(void)
{
	vTaskDelay(pdMS_TO_TICKS(5));
	// 发送起始信号
	I2C_GenerateSTART(I2C2,ENABLE);
	//等待EV5事件
	if(!I2C_WaitEvent(I2C2,I2C_EVENT_MASTER_MODE_SELECT,20000)) return;
	I2C_Send7bitAddress(I2C2,0x70,I2C_Direction_Transmitter);
	if(!I2C_WaitEvent(I2C2,I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED,20000)) return;
	I2C_SendData(I2C2,0xAC);
	if(!I2C_WaitEvent(I2C2,I2C_EVENT_MASTER_BYTE_TRANSMITTED,20000)) return;
	I2C_SendData(I2C2,0x33);
	if(!I2C_WaitEvent(I2C2,I2C_EVENT_MASTER_BYTE_TRANSMITTED,20000)) return;
	I2C_SendData(I2C2,0x00);
	if(!I2C_WaitEvent(I2C2,I2C_EVENT_MASTER_BYTE_TRANSMITTED,20000)) return;
	I2C_GenerateSTOP(I2C2,ENABLE);
}

void AHT30_ContinuousReadData(uint8_t *data)
{
	vTaskDelay(pdMS_TO_TICKS(80));
	I2C_GenerateSTART(I2C2,ENABLE);	
	if(!I2C_WaitEvent(I2C2,I2C_EVENT_MASTER_MODE_SELECT,20000)) return;
	I2C_Send7bitAddress(I2C2,0x70,I2C_Direction_Receiver);
	if(!I2C_WaitEvent(I2C2,I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED,20000)) return;
	for(int i = 0;i<7;i++)
	{
		if(i == 6)
		{
			I2C_AcknowledgeConfig(I2C2,DISABLE);
			I2C_GenerateSTOP(I2C2,ENABLE);
		}
		if(!I2C_WaitEvent(I2C2,I2C_EVENT_MASTER_BYTE_RECEIVED,20000)) return;
		data[i] = I2C_ReceiveData(I2C2);
	}
	I2C_AcknowledgeConfig(I2C2,ENABLE);
	
	if(data[0] & 0x80)
	{
		return;
	}
}


void getAHT_Data(AHT *data)
{
	uint8_t readData[7] = {0};
	AHT30_ContinuousReadData(readData);
	data->status = readData[0];
	data->shidu = ((((uint32_t)readData[1]<<8 | (uint32_t)readData[2]) )<< 4 )| (readData[3] >> 4);
	data->tem = (((uint32_t)(readData[3]&0x0F) << 8 | (uint32_t)readData[4]) << 8)| readData[5] ;
	data->crc = readData[6];
	unsigned char crc = Calc_CRC8((unsigned char*)readData,6);
	
}
