#include "btim.h"

TIM_HandleTypeDef btim_handle = {0};
volatile uint32_t FreeRTOSRunTimeTicks = 0;

/*
 * 给 FreeRTOS 的 vTaskGetRunTimeStats() 提供时间基准：
 *   TIM4 时钟 72MHz，PSC=71 → 1MHz，ARR=99 → 10kHz 中断
 *   每进一次中断 FreeRTOSRunTimeTicks 加 1
 */
void ConfigureTimeForRunTimeStats(void)
{
    __HAL_RCC_TIM4_CLK_ENABLE();

    btim_handle.Instance = TIM4;
    btim_handle.Init.Prescaler = 72 - 1;      /* 72MHz / 72 = 1MHz */
    btim_handle.Init.Period = 100 - 1;        /* 1MHz / 100 = 10kHz，即 0.1ms 分辨率 */
    btim_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    btim_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    btim_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&btim_handle);

    HAL_NVIC_SetPriority(TIM4_IRQn, 15, 0);   /* 最低优先级，不能影响实时任务 */
    HAL_NVIC_EnableIRQ(TIM4_IRQn);
    HAL_TIM_Base_Start_IT(&btim_handle);
}

void TIM4_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&btim_handle, TIM_FLAG_UPDATE) != RESET)
    {
        __HAL_TIM_CLEAR_IT(&btim_handle, TIM_IT_UPDATE);
        FreeRTOSRunTimeTicks++;
    }
}
