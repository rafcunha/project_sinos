/**
 * @file wioe5_port_stm32.h
 * @brief STM32 HAL implementation of wioe5_port_t (the only file of the driver
 *        that touches the HAL).
 *
 * Integration (see driver spec sections 6 and 10.7):
 *
 *   static wioe5_t       wio;
 *   static wioe5_stm32_t wio_hw = { .huart = &huart1,
 *                                   .rst_port = reset_wioe5_GPIO_Port,
 *                                   .rst_pin  = reset_wioe5_Pin };
 *
 *   wioe5_port_t port = wioe5_port_stm32(&wio_hw, &wio);
 *   wioe5_config_default(&cfg);  ...
 *   wioe5_init(&wio, &port, &cfg);
 *   wioe5_port_stm32_start(&wio_hw);       // arms UART RX interrupt
 *
 *   void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h) { wioe5_port_stm32_rx_cplt(&wio_hw, h); }
 *   void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)  { wioe5_port_stm32_error(&wio_hw, h); }
 *
 *   After leaving Stop 2 and restoring clocks: wioe5_port_stm32_resume(&wio_hw);
 */
#ifndef WIOE5_PORT_STM32_H
#define WIOE5_PORT_STM32_H

#include <stdint.h>

#include "stm32u5xx_hal.h"
#include "wioe5.h"

typedef struct {
    UART_HandleTypeDef *huart;    /**< UART wired to the Wio-E5 (9600 8N1)          */
    GPIO_TypeDef       *rst_port; /**< RST pin port (open-drain); NULL if not wired  */
    uint16_t            rst_pin;
    wioe5_t            *dev;         /**< set by wioe5_port_stm32()                  */
    uint8_t             rx_byte;     /**< single-byte interrupt reception buffer      */
    volatile uint32_t   uart_errors; /**< overrun / framing / noise errors seen       */
} wioe5_stm32_t;

#ifdef WIOE5_PORT_TRACE_SIZE
/**
 * Debug trace of the UART traffic (define WIOE5_PORT_TRACE_SIZE, a power of two).
 * TX commands are prefixed with "> ", RX bytes are stored raw. Read it with the
 * debugger: `x/s wioe5_trace.buf` (the buffer wraps; .count is the total written).
 */
typedef struct {
    char              buf[WIOE5_PORT_TRACE_SIZE];
    volatile uint32_t count;
} wioe5_trace_t;
extern wioe5_trace_t wioe5_trace;
#endif

/** Build the port for @p dev. The returned struct is copied by wioe5_init(). */
wioe5_port_t wioe5_port_stm32(wioe5_stm32_t *hw, wioe5_t *dev);

/** Arm byte-by-byte interrupt reception. Call after wioe5_init(). */
HAL_StatusTypeDef wioe5_port_stm32_start(wioe5_stm32_t *hw);

/** Re-arm reception and clear stale error flags after Stop 2. */
void wioe5_port_stm32_resume(wioe5_stm32_t *hw);

/** Forward from HAL_UART_RxCpltCallback (ignores other UARTs). */
void wioe5_port_stm32_rx_cplt(wioe5_stm32_t *hw, UART_HandleTypeDef *huart);

/** Forward from HAL_UART_ErrorCallback (ignores other UARTs). */
void wioe5_port_stm32_error(wioe5_stm32_t *hw, UART_HandleTypeDef *huart);

#endif /* WIOE5_PORT_STM32_H */
