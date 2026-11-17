/**
 * @file wioe5_port_stm32.c
 * @brief STM32 HAL port for the Wio-E5 driver.
 *
 * TX is blocking (HAL_UART_Transmit): the longest command (~500 bytes) takes
 * ~520 ms at 9600 baud. RX keeps running by interrupt meanwhile.
 */
#include "wioe5_port_stm32.h"

#include <stddef.h>

static int port_write(void *ctx, const uint8_t *data, size_t len)
{
    wioe5_stm32_t *hw = (wioe5_stm32_t *)ctx;
    if (len == 0u) {
        return 0;
    }
    if (len > UINT16_MAX) {
        return -1;
    }
    /* ~1.04 ms per byte at 9600 8N1; allow twice that plus margin. */
    uint32_t timeout = (uint32_t)len * 2u + 20u;
    return (HAL_UART_Transmit(hw->huart, data, (uint16_t)len, timeout) == HAL_OK) ? 0 : -1;
}

static uint32_t port_now_ms(void *ctx)
{
    (void)ctx;
    return HAL_GetTick();
}

static void port_set_reset(void *ctx, bool asserted)
{
    wioe5_stm32_t *hw = (wioe5_stm32_t *)ctx;
    /* Open-drain: RESET drives RST low, SET releases it (module pull-up). */
    HAL_GPIO_WritePin(hw->rst_port, hw->rst_pin, asserted ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

wioe5_port_t wioe5_port_stm32(wioe5_stm32_t *hw, wioe5_t *dev)
{
    wioe5_port_t p;
    hw->dev         = dev;
    hw->uart_errors = 0u;
    p.write         = port_write;
    p.now_ms        = port_now_ms;
    p.set_reset     = (hw->rst_port != NULL) ? port_set_reset : NULL;
    p.log           = NULL;
    p.ctx           = hw;
    return p;
}

HAL_StatusTypeDef wioe5_port_stm32_start(wioe5_stm32_t *hw)
{
    return HAL_UART_Receive_IT(hw->huart, &hw->rx_byte, 1u);
}

void wioe5_port_stm32_resume(wioe5_stm32_t *hw)
{
    __HAL_UART_CLEAR_FLAG(hw->huart, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF);
    if (hw->huart->RxState == HAL_UART_STATE_READY) {
        (void)HAL_UART_Receive_IT(hw->huart, &hw->rx_byte, 1u);
    }
}

void wioe5_port_stm32_rx_cplt(wioe5_stm32_t *hw, UART_HandleTypeDef *huart)
{
    if (huart != hw->huart) {
        return;
    }
    wioe5_rx_isr(hw->dev, hw->rx_byte);
    (void)HAL_UART_Receive_IT(hw->huart, &hw->rx_byte, 1u);
}

void wioe5_port_stm32_error(wioe5_stm32_t *hw, UART_HandleTypeDef *huart)
{
    if (huart != hw->huart) {
        return;
    }
    hw->uart_errors++;
    __HAL_UART_CLEAR_FLAG(hw->huart, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF);
    if (hw->huart->RxState == HAL_UART_STATE_READY) {
        (void)HAL_UART_Receive_IT(hw->huart, &hw->rx_byte, 1u);
    }
}
