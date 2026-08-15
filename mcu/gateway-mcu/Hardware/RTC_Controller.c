#include "stm32f10x.h"                  // Device header
#include "RTC_Controller.h"
#include "Delay.h"
#include <stdio.h>

//初始化RTC时钟，作为系统时钟

void RTC_CTRL_Init(void){
	//======注意1.对后备寄存器的访问是被禁止的
	// 所以使能 PWR 和 BKP 时钟，这是访问后备域的前提
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP | RCC_APB1Periph_PWR , ENABLE);
	PWR_BackupAccessCmd(ENABLE);
	
	//如果之前设置过值，那就只继续同步就行，没设置过就会执行else语句
	if(RTC_GetCounter() > 0){
		//该函数会等待直到同步完成，确保后续读取 RTC 寄存器值的准确性
		RTC_WaitForSynchro();
		
	}else{
		//1.开启动LSE（外部32.768kHz晶振）
		RCC_LSEConfig(RCC_LSE_ON);
		//注意：部分开发板没有32.768kHz晶振，LSE永远不会就绪，
		//等待带超时，超时后回退到LSI内部时钟，避免卡死
		uint32_t t = 0;
		while(RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET){
			if(++t > 3000){
				break;
			}
			Delay_ms(1);
		}
		
		if(RCC_GetFlagStatus(RCC_FLAG_LSERDY) == SET){
			//2.LSE选择RTC时钟
			RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
			RCC_RTCCLKCmd(ENABLE);
			
			//======注意2.APBI时钟和RTC时钟要同步一次，APB1管写入PSC和CNT
			RTC_WaitForSynchro();//已经写好的函数
			
			/* 注意3: RTC要进入配置模式，才可以设置RTC_PRL、RTC_CNT、RTC_ALR
			  下面两个函数的原码里，都有写了，所以不用写 	*/
			
			//3.配置RTC时钟(预分频和计数器)
			//注意4.等待上一次操作完成
			RTC_WaitForLastTask();
			RTC_SetPrescaler(32768 - 1);
			RTC_WaitForLastTask();
		}else{
			//回退：LSI内部时钟（约40kHz，走时会偏，但不会卡死）
			RCC_LSICmd(ENABLE);
			while(RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET);
			RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
			RCC_RTCCLKCmd(ENABLE);
			RTC_WaitForSynchro();
			RTC_WaitForLastTask();
			RTC_SetPrescaler(32768 - 1);
			RTC_WaitForLastTask();
		}
		
		RTC_SetCounter(0);
		RTC_WaitForLastTask();
	}
}
	
//设置闹钟
void setAlarm(uint32_t alarmValue){
	
	RTC_SetAlarm(alarmValue);//设置闹钟的值
	
	RTC_WaitForLastTask();//等待前一次执行结束
}


void setCounter(int32_t counter){
	RTC_WaitForLastTask();
	RTC_SetCounter(counter);
	
	RTC_WaitForLastTask();
}

/* 将Unix时间戳（秒）转换为 "YYYY-MM-DD HH:MM:SS" 字符串。
 * tzHour：时区（北京时间 = 8）。 */
void UnixTimeToStr(uint32_t unixtime, int8_t tzHour, char *out)
{
	uint32_t t = unixtime + (uint32_t)tzHour * 3600;
	uint32_t days = t / 86400;
	uint32_t rem  = t % 86400;
	uint32_t hh = rem / 3600; rem %= 3600;
	uint32_t mm = rem / 60;
	uint32_t ss = rem % 60;

	/* civil_from_days 算法（Howard Hinnant），把1970年起的天数换算成日期 */
	uint32_t z = days + 719468;
	uint32_t era = z / 146097;
	uint32_t doe = z - era * 146097;
	uint32_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
	uint32_t y = yoe + era * 400;
	uint32_t doy = doe - (365*yoe + yoe/4 - yoe/100);
	uint32_t mp = (5*doy + 2)/153;
	uint32_t d = doy - (153*mp+2)/5 + 1;
	uint32_t m = mp < 10 ? mp+3 : mp-9;
	y += (m <= 2);

	sprintf(out, "%04u-%02u-%02u %02u:%02u:%02u", y, m, d, hh, mm, ss);
}
