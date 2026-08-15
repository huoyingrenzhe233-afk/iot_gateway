#include "Zigbee.h"

QueueHandle_t Zigbee_Queue;


void Zigbee_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* =========================
       1. 开启时钟
       ========================= */

    RCC_APB2PeriphClockCmd(
        RCC_APB2Periph_GPIOA,
        ENABLE
    );

    RCC_APB1PeriphClockCmd(
        RCC_APB1Periph_USART2,
        ENABLE
    );


    /* =========================
       2. USART2 TX
       PA2
       ========================= */

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(GPIOA, &GPIO_InitStructure);


    /* =========================
       3. USART2 RX
       PA3
       ========================= */

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;

    GPIO_Init(GPIOA, &GPIO_InitStructure);


    /* =========================
       4. USART2 参数
       ========================= */

    USART_InitStructure.USART_BaudRate = 115200;

    USART_InitStructure.USART_WordLength =
        USART_WordLength_8b;

    USART_InitStructure.USART_StopBits =
        USART_StopBits_1;

    USART_InitStructure.USART_Parity =
        USART_Parity_No;

    USART_InitStructure.USART_HardwareFlowControl =
        USART_HardwareFlowControl_None;

    USART_InitStructure.USART_Mode =
        USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(
        USART2,
        &USART_InitStructure
    );


    /* =========================
       5. 开启接收中断
       ========================= */

    USART_ITConfig(
        USART2,
        USART_IT_RXNE,
        ENABLE
    );


    /* =========================
       6. NVIC
       ========================= */

    NVIC_PriorityGroupConfig(
        NVIC_PriorityGroup_4
    );

    NVIC_InitStructure.NVIC_IRQChannel =
        USART2_IRQn;

    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority =
        5;

    NVIC_InitStructure.NVIC_IRQChannelSubPriority =
        0;

    NVIC_InitStructure.NVIC_IRQChannelCmd =
        ENABLE;

    NVIC_Init(&NVIC_InitStructure);


    /* =========================
       7. 创建 Zigbee 队列
       ========================= */

    Zigbee_Queue =
        xQueueCreate(
            128,
            sizeof(uint8_t)
        );


    /* =========================
       8. 开启 USART2
       ========================= */

    USART_Cmd(
        USART2,
        ENABLE
    );
}


void USART2_IRQHandler(void)
{
    uint8_t data;

    if(USART_GetITStatus(
            USART2,
            USART_IT_RXNE) != RESET)
    {
        data = USART_ReceiveData(USART2);

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        xQueueSendFromISR(
            Zigbee_Queue,
            &data,
            &xHigherPriorityTaskWoken
        );

        portYIELD_FROM_ISR(
            xHigherPriorityTaskWoken
        );
    }
}


void Zigbee_SendByte(uint8_t data)
{
    USART_SendData(USART2, data);

    while(
        USART_GetFlagStatus(
            USART2,
            USART_FLAG_TXE
        ) == RESET
    );
}

void Zigbee_SendString(char *str)
{
    while(*str)
    {
        Zigbee_SendByte(*str++);
    }
}