#include "stm32h7xx_it.h"

#include "audio_input.h"
#include "stm32h7xx_hal.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
    HAL_SYSTICK_IRQHandler();
}

void DMA1_Stream0_IRQHandler(void)
{
    audio_input_dma_irq_handler();
}
