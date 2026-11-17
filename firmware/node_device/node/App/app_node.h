/**
 * @file app_node.h
 * @brief STM32 glue: wires USART1 / PA0 to the Wio-E5 driver and runs the
 *        LoRaWAN uplink cycle. Called from main.c (USER CODE sections only).
 */
#ifndef APP_NODE_H
#define APP_NODE_H

#include "stm32u5xx_hal.h"

/** Call once after MX_USART1_UART_Init(). */
void app_node_init(UART_HandleTypeDef *huart);

/** Call on every iteration of the main loop. Idles the MCU when possible. */
void app_node_process(void);

#endif /* APP_NODE_H */
