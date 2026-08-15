#include "stm32f10x.h"                  // Device header

/**
	延迟us的函数
*/
void Delay_us(uint32_t us)
{
	SysTick->LOAD = 72 * us;//设置重装寄存器 72000
	SysTick->VAL = 0 ;//计数器
	SysTick->CTRL = 0x00000005;//控制状态寄存器
	//等待STK_CTRL寄存器的16位变成1，意味着计数器减到了0
	while(!(SysTick->CTRL & 0x00010000));
	SysTick->CTRL = 0x00000004;//关闭计数器
}

/**
	延迟ms的函数
*/
void Delay_ms(uint32_t ms)
{
	while(ms--)
	{
		Delay_us(1000);
	}
}

/**
	延迟s的函数
*/
void Delay_s(uint32_t s)
{
	while(s--)
	{
		Delay_ms(1000);
	}
}