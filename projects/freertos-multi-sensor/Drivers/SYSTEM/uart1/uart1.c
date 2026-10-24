#include "sys.h"
#include "uart1.h"
#include "string.h"

UART_HandleTypeDef uart1_handle = {0};          /* UART1 句柄 */
DMA_HandleTypeDef  hdma_usart1_rx = {0};        /* UART1_RX 对应的 DMA */
QueueHandle_t      uart1_rx_queue = NULL;       /* 收完整帧后往这里投递 */

static uint8_t  uart1_dma_buf[UART1_RX_BUF_SIZE];  /* DMA 循环搬运的缓冲区 */
static uint16_t uart1_last_pos = 0;                /* 上一次已经取走的 DMA 位置 */

/* printf 最终会调用 fputc 把每个字符发到串口 */
int fputc(int ch, FILE *f)
{
    while ((USART1->SR & 0X40) == 0);
    USART1->DR = (uint8_t)ch;
    return ch;
}

void uart1_init(uint32_t baudrate)
{
    uart1_handle.Instance = USART1;
    uart1_handle.Init.BaudRate = baudrate;
    uart1_handle.Init.WordLength = UART_WORDLENGTH_8B;
    uart1_handle.Init.StopBits = UART_STOPBITS_1;
    uart1_handle.Init.Parity = UART_PARITY_NONE;
    uart1_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart1_handle.Init.Mode = UART_MODE_TX_RX;
    HAL_UART_Init(&uart1_handle);

    /* 创建“收帧队列”：最多缓存 4 帧，每帧 UartFrame_t 大小 */
    uart1_rx_queue = xQueueCreate(4, sizeof(UartFrame_t));

    /* 启动 DMA + 空闲中断接收（循环模式，不需要每帧重启） */
    uart1_last_pos = 0;
    HAL_UART_Receive_DMA(&uart1_handle, uart1_dma_buf, UART1_RX_BUF_SIZE);
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio = {0};

    if (huart->Instance == USART1)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_DMA1_CLK_ENABLE();

        /* PA9 = TX，复用推挽；PA10 = RX，浮空/上拉输入 */
        gpio.Pin   = GPIO_PIN_9;
        gpio.Mode  = GPIO_MODE_AF_PP;
        gpio.Pull  = GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &gpio);

        gpio.Pin  = GPIO_PIN_10;
        gpio.Mode = GPIO_MODE_AF_INPUT;
        HAL_GPIO_Init(GPIOA, &gpio);

        /* USART1_RX 对应 DMA1_Channel5 */
        hdma_usart1_rx.Instance = DMA1_Channel5;
        hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        hdma_usart1_rx.Init.Mode = DMA_CIRCULAR;      /* 循环模式，配合空闲中断 */
        hdma_usart1_rx.Init.Priority = DMA_PRIORITY_HIGH;
        HAL_DMA_Init(&hdma_usart1_rx);
        __HAL_LINKDMA(huart, hdmarx, hdma_usart1_rx);

        /* 只需要空闲中断：一帧结束才进中断，不用每字节都打断 CPU */
        HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);
    }
}

/* 串口空闲中断：DMA 已经收完一帧，把数据从循环缓冲区取出来放进队列 */
void USART1_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;

    if (__HAL_UART_GET_FLAG(&uart1_handle, UART_FLAG_IDLE) != RESET)
    {
        UartFrame_t frame;
        uint16_t pos, len = 0, tail;

        __HAL_UART_CLEAR_IDLEFLAG(&uart1_handle);

        /* DMA 剩余计数 → 当前写到循环缓冲区的哪个位置 */
        pos = (uint16_t)(UART1_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart1_rx));
        if (pos >= UART1_RX_BUF_SIZE)
            pos = 0;

        if (pos != uart1_last_pos)
        {
            /* 没有回绕：直接拷；回绕：先拷尾部再拷头部 */
            if (pos > uart1_last_pos)
            {
                len = pos - uart1_last_pos;
                if (len > UART1_FRAME_MAX)
                    len = UART1_FRAME_MAX;
                memcpy(frame.data, &uart1_dma_buf[uart1_last_pos], len);
            }
            else
            {
                tail = UART1_RX_BUF_SIZE - uart1_last_pos;
                if (tail > UART1_FRAME_MAX)
                    tail = UART1_FRAME_MAX;
                memcpy(frame.data, &uart1_dma_buf[uart1_last_pos], tail);
                len = tail;
                if (len < UART1_FRAME_MAX && pos > 0)
                {
                    uint16_t head = pos;
                    if (len + head > UART1_FRAME_MAX)
                        head = UART1_FRAME_MAX - len;
                    memcpy(&frame.data[len], uart1_dma_buf, head);
                    len += head;
                }
            }

            uart1_last_pos = pos;

            if (len > 0)
            {
                frame.len = len;
                xQueueSendFromISR(uart1_rx_queue, &frame, &woken);
            }
        }
    }

    portYIELD_FROM_ISR(woken);
}

BaseType_t uart1_wait_frame(UartFrame_t *frame, TickType_t timeout)
{
    if (uart1_rx_queue == NULL)
        return pdFALSE;
    return xQueueReceive(uart1_rx_queue, frame, timeout);
}
