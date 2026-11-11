#include "stm32l0xx_hal.h"
#include "wire.h"

#include <string.h>

enum { RX_CAPACITY = 128 };
static UART_HandleTypeDef uart;
static uint8_t receive_byte;
static uint8_t receive_ring[RX_CAPACITY];
static volatile uint8_t receive_head;
static volatile uint8_t receive_tail;
static volatile bool receive_overflow;

static void require_hal(HAL_StatusTypeDef status)
{
    if (status == HAL_OK) {
        return;
    }
    __disable_irq();
    for (;;) {
        __WFI();
    }
}

static void init_clock(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    oscillator.PLL.PLLState = RCC_PLL_NONE;
    require_hal(HAL_RCC_OscConfig(&oscillator));

    RCC_ClkInitTypeDef clock = {0};
    clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV1;
    clock.APB2CLKDivider = RCC_HCLK_DIV1;
    require_hal(HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_1));

    RCC_PeriphCLKInitTypeDef peripheral = {0};
    peripheral.PeriphClockSelection = RCC_PERIPHCLK_USART2;
    peripheral.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
    require_hal(HAL_RCCEx_PeriphCLKConfig(&peripheral));
}

static void init_uart(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    GPIO_InitTypeDef pins = {0};
    pins.Pin = GPIO_PIN_2 | GPIO_PIN_15;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    pins.Alternate = GPIO_AF4_USART2;
    HAL_GPIO_Init(GPIOA, &pins);

    uart.Instance = USART2;
    uart.Init.BaudRate = 115200;
    uart.Init.WordLength = UART_WORDLENGTH_8B;
    uart.Init.StopBits = UART_STOPBITS_1;
    uart.Init.Parity = UART_PARITY_NONE;
    uart.Init.Mode = UART_MODE_TX_RX;
    uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart.Init.OverSampling = UART_OVERSAMPLING_16;
    uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    require_hal(HAL_UART_Init(&uart));
    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    require_hal(HAL_UART_Receive_IT(&uart, &receive_byte, 1));
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&uart);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    const uint8_t next = (uint8_t)((receive_head + 1u) % RX_CAPACITY);
    if (next == receive_tail) {
        receive_overflow = true;
    } else {
        receive_ring[receive_head] = receive_byte;
        receive_head = next;
    }
    HAL_UART_Receive_IT(handle, &receive_byte, 1);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    receive_overflow = true;
    if (handle->RxState == HAL_UART_STATE_READY) {
        HAL_UART_Receive_IT(handle, &receive_byte, 1);
    }
}

void io_send(const char *line)
{
    uint8_t frame[80];
    const size_t length = strlen(line);
    if (length >= sizeof(frame)) {
        return;
    }
    memcpy(frame, line, length);
    frame[length] = '\n';
    HAL_UART_Transmit(&uart, frame, (uint16_t)(length + 1), 20);
}

#ifdef ACTUATOR
static TIM_HandleTypeDef servo_pwm;
enum { SERVO_MIN_US = 500, SERVO_MAX_US = 2400 };

static void init_outputs(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_RESET);

    GPIO_InitTypeDef pins = {0};
    pins.Mode = GPIO_MODE_OUTPUT_PP;
    pins.Speed = GPIO_SPEED_FREQ_LOW;
    pins.Pin = GPIO_PIN_0 | GPIO_PIN_1;  /* D3 / DIR, D6 / STEP */
    HAL_GPIO_Init(GPIOB, &pins);
    pins.Pin = GPIO_PIN_11;             /* D10 / pump LED */
    HAL_GPIO_Init(GPIOA, &pins);

    pins.Pin = GPIO_PIN_8;              /* D9 / servo PWM */
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Alternate = GPIO_AF5_TIM2;
    HAL_GPIO_Init(GPIOA, &pins);

    servo_pwm.Instance = TIM2;
    servo_pwm.Init.Prescaler = 15;      /* 16 MHz / 16 = 1 MHz */
    servo_pwm.Init.CounterMode = TIM_COUNTERMODE_UP;
    servo_pwm.Init.Period = 19999;      /* 20,000 µs = 50 Hz */
    servo_pwm.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    require_hal(HAL_TIM_PWM_Init(&servo_pwm));

    TIM_OC_InitTypeDef channel = {0};
    channel.OCMode = TIM_OCMODE_PWM1;
    channel.Pulse = SERVO_MIN_US;
    channel.OCPolarity = TIM_OCPOLARITY_HIGH;
    require_hal(HAL_TIM_PWM_ConfigChannel(&servo_pwm, &channel, TIM_CHANNEL_1));
    require_hal(HAL_TIM_PWM_Start(&servo_pwm, TIM_CHANNEL_1));
}

void io_servo(int angle)
{
    const uint32_t pulse = SERVO_MIN_US +
                           (uint32_t)angle * (SERVO_MAX_US - SERVO_MIN_US) / 180;
    __HAL_TIM_SET_COMPARE(&servo_pwm, TIM_CHANNEL_1, pulse);
}

void io_direction(bool forward)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, forward ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void io_step(bool high)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void io_pump(bool active)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, active ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
#endif

int main(void)
{
    require_hal(HAL_Init());
    init_clock();
#ifdef ACTUATOR
    init_outputs();
#endif
    init_uart();
    app_init();
    FrameReader reader = {0};
    for (;;) {
        if (receive_overflow) {
            __disable_irq();
            receive_tail = receive_head;
            receive_overflow = false;
            __enable_irq();
            reader.discard = true;
        }
        /* A continuous UART stream must not starve the actuator scheduler. */
        for (unsigned processed = 0;
             processed < RX_CAPACITY && receive_tail != receive_head && !receive_overflow;
             ++processed) {
            const uint8_t byte = receive_ring[receive_tail];
            receive_tail = (uint8_t)((receive_tail + 1u) % RX_CAPACITY);
            frame_byte(&reader, byte, HAL_GetTick());
        }
        app_tick(HAL_GetTick());
        __WFI();
    }
}
