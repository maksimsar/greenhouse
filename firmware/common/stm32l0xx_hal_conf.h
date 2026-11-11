#ifndef STM32L0XX_HAL_CONF_H
#define STM32L0XX_HAL_CONF_H

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

#define HSI_VALUE 16000000U
#define HSE_VALUE 8000000U
#define HSE_STARTUP_TIMEOUT 100U
#define MSI_VALUE 2097000U
#define LSI_VALUE 37000U
#define LSE_VALUE 32768U
#define LSE_STARTUP_TIMEOUT 5000U
#define HSI48_VALUE 48000000U
#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 3U
#define USE_RTOS 0U
#define PREFETCH_ENABLE 0U
#define PREREAD_ENABLE 0U
#define BUFFER_CACHE_DISABLE 0U
#define USE_HAL_TIM_REGISTER_CALLBACKS 0U
#define USE_HAL_UART_REGISTER_CALLBACKS 0U

#include "stm32l0xx_hal_rcc.h"
#include "stm32l0xx_hal_gpio.h"
#include "stm32l0xx_hal_dma.h"
#include "stm32l0xx_hal_cortex.h"
#include "stm32l0xx_hal_flash.h"
#include "stm32l0xx_hal_pwr.h"
#include "stm32l0xx_hal_tim.h"
#include "stm32l0xx_hal_uart.h"

#define assert_param(expr) ((void)0U)

#endif
