#include "stm32f10x.h"                  // Device header
#include "USART_Model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "RTC_Controller.h"
#include "ESP8266.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "BreathLED.h"
#include "dianji.h"
#include "Buzzer.h"
#include "TempSensor.h"
#include "RedSensor.h"
#include "AHT30.h"
#include "queue.h"
#include "semphr.h"
#include "ParseCmd.h"
#include "AHT30.h"
#include "RTC_Controller.h"
#include "ADC.h"
#include "LED.h"
#include "ZigBee.h"


volatile uint8_t connectstate = 0;
volatile uint8_t mqttState = 0;
QueueHandle_t Cmd_Queue;
QueueHandle_t Zigbee_Report_Queue;

SemaphoreHandle_t MQTT_Mutex;
TaskHandle_t BreathHandler;

TaskHandle_t connectWifiHandler;

TaskHandle_t MQTTHandler;

TaskHandle_t dianjiHandler;

TaskHandle_t LEDControlHandler;

TaskHandle_t HardwareHandler;

TaskHandle_t collectHandler;

QueueHandle_t Result_Queue;

TaskHandle_t MQTTReturnHandler;

TaskHandle_t ZigBeeHandler;

void connectWifiTask(void *param)
{
    uint8_t ok = CloseATEAndConnectWifi();

    if(ok)
    {
        uint32_t t = 0;

        // 尝试同步时间
        for(int i = 0; i < 3; i++)
        {
            t = SynchronizeTime();

            if(t > 0)
            {
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // 只有拿到有效时间，才设置RTC
        if(t > 0)
        {
            setCounter((int32_t)t);
        }
    }

    connectstate = ok;
    vTaskDelete(NULL);
}
void MQTTTask(void *param)
{
    uint8_t flag = 0;
	
    while(1)
    {
		if(connectstate == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
		
		
        if(flag == 0)
        {
            flag = connectMQTT(5000);

            if(flag)
            {
				mqttState = 1;
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
        }
        else
        {
            Cmd_t cmd;
            char msgCopy[BUFFER_SIZE];

            /* MQTT 掉线检测：ESP8266 输出断开事件时重置连接标志并重连。
             * 否则断开后本任务会一直停在解析分支，收不到指令、数据也推不出去 */
            if(strstr(message, "MQTTDISCONNECT") != NULL ||
               strstr(message, "WIFI DISCONNECT") != NULL ||
               strstr(message, "MQTTUNEVENT") != NULL)
            {
                flag = 0;
                mqttState = 0;
                memset(message, 0, BUFFER_SIZE);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            /* 关中断保护下拷贝到本地再解析：
             * publishMqtt/SendAT 会 memset(message)，USART1 IDLE 中断也会改写 message，
             * 直接在共享缓冲上解析会导致命令被并发清掉/覆盖而丢失 */
            taskENTER_CRITICAL();
            memcpy(msgCopy, message, BUFFER_SIZE);
            taskEXIT_CRITICAL();

            cmd = ParseCmd(msgCopy);

            if(cmd.valid != 0)
            {
                /* 先入本机控制队列，成功后才转发给 ZigBee 并清空 message，
                 * 避免队列满时下轮重复解析、重复转发到 ZigBee */
                if(xQueueSend(Cmd_Queue, &cmd, 0) == pdPASS)
                {
                    /* 下行转发：body.transport == "zigbee" 的指令，把完整 JSON 帧发给 ZigBee 模块 */
                    if(cmd.transport != 0)
                    {
                        char zigJson[256];

                        if(ExtractJson(msgCopy, zigJson, sizeof(zigJson)) > 0)
                        {
                            Zigbee_SendString(zigJson);
                        }
                    }

                    memset(message, 0, BUFFER_SIZE);
                }
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}


void BreathLEDTask(void* param){
	
	uint8_t flag = 0;
	for(int i = 0;i<100;){
		if(i ==  99){
			flag = !flag;
			i = 0;
		}
		if(!flag){
			TIM_SetCompare1(TIM2,100*(i++));
			vTaskDelay(10);
		}
		if(flag){
			TIM_SetCompare1(TIM2,10000-100*(i++));
			vTaskDelay(10);
		}
	}
}


void dianjiTask(void* param){
	while(1){
	 vTaskDelay(pdMS_TO_TICKS(10));
	}	
}

void HardwareTask(void *param)
{
    Cmd_t cmd;
	Result_t result;
    while(1)
    {
        if(xQueueReceive(Cmd_Queue, &cmd, portMAX_DELAY) == pdPASS)
        {
           if(cmd.led_on == 1){
			   TIM_SetCompare1(TIM3,cmd.led_br*100);

		   }else{
				 TIM_SetCompare1(TIM3,0);
		   }
		   if(cmd.motor_on == 1){
			   if(cmd.motor_dir == 1){
							
				GPIO_SetBits(GPIOA,GPIO_Pin_11);
				GPIO_ResetBits(GPIOA,GPIO_Pin_12);
				TIM_SetCompare2(TIM2,cmd.motor_sp*100);
			   }else{
			    GPIO_SetBits(GPIOA,GPIO_Pin_12);
				GPIO_ResetBits(GPIOA,GPIO_Pin_11);
				TIM_SetCompare2(TIM2,cmd.motor_sp*100);
			   }
		   }else{
				TIM_SetCompare2(TIM2,0);
		   }
		   if(cmd.buzzer == 1){
				GPIO_ResetBits(GPIOA,GPIO_Pin_7);
		   }else{
				GPIO_SetBits(GPIOA,GPIO_Pin_7);
		   }
		   
			result.result = 1;
            result.led_on = cmd.led_on;
            result.motor_on = cmd.motor_on;
            result.buzzer = cmd.buzzer;
            xQueueSend(Result_Queue, &result, 0);
        }
    }
}
AHT data = {0};


void publishMqtt(char *topic, char *json)
{
    char pubRaw[128];

    if(xSemaphoreTake(MQTT_Mutex, portMAX_DELAY) == pdTRUE)
    {
        sprintf(pubRaw,
                "AT+MQTTPUBRAW=0,%s,%d,1,0\r\n",
                topic,
                (int)strlen(json));

        if(!SendAT(pubRaw, ">", 5000))
        {
            /* 失败也必须释放 Mutex */
            xSemaphoreGive(MQTT_Mutex);
            return;
        }

        Send_String(json);

        if(!WaitAT("OK", 2000))
        {
        }

        /* 正常发送完成，释放 Mutex */
        xSemaphoreGive(MQTT_Mutex);
    }
}

void gettime(char ts[24]){
	/* 用RTC的真实时间生成时间戳（北京时间UTC+8） */
		
		uint32_t now = RTC_GetCounter();
		if(now > 0){
			UnixTimeToStr(now, 8, ts);
		}else{
			strcpy(ts, "1970-01-01 00:00:00");
		}
}


void MQTTReturnTask(void* param){
	Result_t result;
    char mqtt_msg[256];
	char ts[24];
    char zigbee_report[360];
    while(1)
    {
        /* 轮询两个上报队列：本机状态结果 + ZigBee 上行数据，避免阻塞任一发布路径 */
        if(xQueueReceive(Result_Queue, &result, 20) == pdPASS)
        {
			
			gettime(ts);
             /* 生成状态 JSON */
            sprintf(mqtt_msg,
                    "{\"type\":\"status\","
                    "\"dev\":\"mcu01\","
                    "\"ts\":\"%s\","
                    "\"body\":{\"items\":["
                    "{\"name\":\"led\",\"state\":\"%s\",\"value\":%d},"
                    "{\"name\":\"motor\",\"state\":\"%s\",\"value\":%d},"
                    "{\"name\":\"buzzer\",\"state\":\"%s\",\"value\":%d}"
                    "]}}",
                    ts,

                    result.led_on ? "on" : "off",
                    TIM_GetCapture3(TIM2),

                    result.motor_on ? "on" : "off",
                    TIM_GetCapture2(TIM2),

                    result.buzzer ? "on" : "off",
                    result.buzzer
            );

            publishMqtt("\"dev/mcu01/report\"",mqtt_msg);
        }

        if(xQueueReceive(Zigbee_Report_Queue, zigbee_report, 20) == pdPASS)
        {
            publishMqtt("\"dev/mcu01/report\"", zigbee_report);
        }
    }

}




void collectTask(void* param){
	char json[200];
	char ts[24];
	float tem = 27.6f;
	float humi = 36.1f;
	int light = 320;
	int ir = 2500;
	while(1){
		if(mqttState != 1){
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}
		AHT30_WriteData();
		getAHT_Data(&data);
		humi = (float)data.shidu/1048576*100;
		tem = (float)data.tem/1048576*200-50;
		
		int32_t temint = (int32_t)tem;
		int32_t temdec = (tem - temint)*10;
		
		int32_t shiduint = (int32_t)humi;
		int32_t shidudec = (humi - shiduint)*10;
		gettime(ts);

		/* 先生成 JSON */
		sprintf(json,
				"{\"type\":\"sensor\","
				"\"dev\":\"mcu01\","
				"\"ts\":\"%s\","
				"\"body\":{\"data\":{"
				"\"temp\":%.1f,"
				"\"humi\":%.1f,"
				"\"light\":%d,"
				"\"ir\":%d"
				"}}}",
				ts,
				tem,
				humi,
				4095-DATA[0],
				DATA[1]);

		publishMqtt("\"dev/mcu01/report\"",json);
		
		
		vTaskDelay(pdMS_TO_TICKS(1000));
		
	}
}

/* 解析一帧 ZigBee 收到的 JSON 指令，示例：
 * {"type":"chsw","dev":"mcu01","ts":"2026-08-14 15:48:08","body":{"transport":"zigbee"}}
 * 解析后封装成 {"type":"zigbee",...,"body":{"data":<原始JSON>}} 上报云端 */
static void HandleZigbeeFrame(char *frame)
{
    cJSON *root = cJSON_Parse(frame);
    if(root == NULL)
    {
        /* 解析失败：直接丢弃，不再打印（printf 会经 USART1 打到 ESP8266，干扰其 AT 状态机） */
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");

    /* ZigBee 链路下行控制：type=cmd 的指令解析后送 Cmd_Queue，
     * 由 HardwareTask 执行 LED/电机/蜂鸣器控制（与 MQTT 下行共用同一处理链）。
     * 例如 {"type":"cmd",...,"body":{"led_on":1,"led_br":58,...}} */
    if(cJSON_IsString(type) && strcmp(type->valuestring, "cmd") == 0)
    {
        Cmd_t cmd = ParseCmd(frame);

        if(cmd.valid != 0)
        {
            xQueueSend(Cmd_Queue, &cmd, 0);
        }
    }

    /* 注：此处不再用 printf 打印帧内容——printf 经 fputc 重定向到 USART1，
     * 会把调试文本直接打进 ESP8266 的 RX，干扰 AT/MQTT 状态机，导致 MQTT 收发异常 */
    (void)type;

    /* 上行上报：封装成带 ts 的 MQTT 消息（frame 本身是合法 JSON 对象，直接嵌入 body.data）。
     * 只投队列、不在本任务阻塞 publishMqtt，避免 Zigbee_Queue 背压丢帧。 */
    if(mqttState == 1)
    {
        char ts[24];
        char mqtt_msg[360];

        gettime(ts);
        sprintf(mqtt_msg,
                "{\"type\":\"zigbee\","
                "\"dev\":\"mcu01\","
                "\"ts\":\"%s\","
                "\"body\":{\"data\":%s}}",
                ts, frame);

        xQueueSend(Zigbee_Report_Queue, mqtt_msg, 0);
    }

    cJSON_Delete(root);
}

void ZigBeeTask(void* param){
    /* 按 JSON 完整性拆帧：'{' 开始累积，'}' 配对闭合即一帧 */
    char frame[256];
    uint16_t len = 0;
    int depth = 0;
    uint8_t data;

    while(1)
    {
        if(xQueueReceive(Zigbee_Queue, &data, portMAX_DELAY) == pdPASS)
        {
            char c = (char)data;

            /* 帧外：等待 '{' 开始一帧 */
            if(depth == 0)
            {
                if(c == '{')
                {
                    depth = 1;
                    len = 0;
                    frame[len++] = c;
                }
                continue;
            }

            /* 帧内：累积字节并跟踪 {} 深度，深度回到 0 即一帧完整。
             * 缓冲满（len 达到 sizeof(frame)-1）时整帧作废，不再触发处理，
             * 避免发出缺末尾 '}' 的坏帧。 */
            if(len < sizeof(frame) - 1)
            {
                frame[len++] = c;
            }

            if(c == '{')
            {
                depth++;
            }
            else if(c == '}')
            {
                depth--;
                if(depth == 0)
                {
                    if(len >= sizeof(frame) - 1)
                    {
                        /* 缓冲满丢尾：整帧作废 */
                        len = 0;
                    }
                    else
                    {
                        frame[len] = '\0';
                        HandleZigbeeFrame(frame);
                        len = 0;
                    }
                }
            }

            /* 缓冲满仍未闭合：丢弃，重新等下一帧 */
            if(len >= sizeof(frame) - 1 && depth > 0)
            {
                depth = 0;
                len = 0;
            }
        }
    }
}
int main()
{
    //=====初始化=====
	USART1_Init();
	RTC_CTRL_Init();
	Zigbee_Init();
	BreathLEDInit();
	LED_PWM();
	dianjiInit();
	BuzzerInit();
	AHT30Init();
	ADCInit();
	
	Cmd_Queue = xQueueCreate(10, sizeof(Cmd_t));
	
	Result_Queue = xQueueCreate(5, sizeof(Result_t));
    Zigbee_Report_Queue = xQueueCreate(3, 360);
	MQTT_Mutex = xSemaphoreCreateMutex();

    /* cJSON 的 malloc/free 切到 FreeRTOS heap_4（内部 vTaskSuspendAll 互斥），
     * 避免 MQTTTask/ZigBeeTask/connectWifiTask 多任务并发调用 newlib malloc 损坏堆 */
    {
        cJSON_Hooks hooks;
        hooks.malloc_fn = pvPortMalloc;
        hooks.free_fn = vPortFree;
        cJSON_InitHooks(&hooks);
    }
	
	
	xTaskCreate(BreathLEDTask,"BreathLED",128,NULL,2,&BreathHandler);
	xTaskCreate(ZigBeeTask,"ZIGBEE",512,NULL,2,&ZigBeeHandler);
	xTaskCreate(connectWifiTask,"WIFI",512,NULL,3,&connectWifiHandler);
	
	xTaskCreate(MQTTTask,"MQTT",512,NULL,4,&MQTTHandler);
	
	xTaskCreate(collectTask,"Collection",512,NULL,1,&collectHandler);
   
	
	xTaskCreate(HardwareTask,"Hardware",128,NULL,2,&HardwareHandler);
	xTaskCreate(MQTTReturnTask,"ReturnMQTT",512,NULL,2,&MQTTReturnHandler);
	
	
	
	
	// 启动调度器
    vTaskStartScheduler();
    
    while(1)
    {
        // 不会执行到这里
		
    }
}



