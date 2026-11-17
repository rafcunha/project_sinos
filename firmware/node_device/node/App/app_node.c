/**
 * @file app_node.c
 * @brief STM32 glue for the node: driver instance, HAL callbacks, payload and
 *        idle policy.
 *
 * Low power: there is no RTC/LPTIM in the CubeMX project yet, so between
 * uplinks the MCU only enters Sleep mode (__WFI, woken by SysTick every 1 ms).
 * Moving to Stop 2 (driver spec 10.7) needs an RTC wakeup timer; the hook is
 * idle_mcu() below, gated by wioe5_can_stop().
 */
#include "app_node.h"

#include <string.h>

#include "app_lorawan.h"
#include "main.h"
#include "wioe5.h"
#include "wioe5_port_stm32.h"

/* ---- Instances ----------------------------------------------------------- */

static wioe5_t       s_wio;
static wioe5_stm32_t s_wio_hw;
static app_lorawan_t s_app;
static uint16_t      s_heartbeat;

/* ---- Driver callbacks ---------------------------------------------------- */

static void on_event(void *user, wioe5_event_t evt)
{
    app_lorawan_on_event((app_lorawan_t *)user, evt);
}

static void on_downlink(void *user, const wioe5_downlink_t *dl)
{
    app_lorawan_on_downlink((app_lorawan_t *)user, dl);
}

static uint32_t clock_ms(void *ctx)
{
    (void)ctx;
    return HAL_GetTick();
}

/**
 * Payload until the ultrasonic sensor driver exists: a 3-byte heartbeat
 * [0x00 = heartbeat type][counter MSB][counter LSB]. It proves the link end to
 * end without inventing a river level.
 */
static size_t heartbeat_payload(void *user, uint8_t *buf, size_t cap)
{
    (void)user;
    if (cap < 3u) {
        return 0u;
    }
    s_heartbeat++;
    buf[0] = 0x00u;
    buf[1] = (uint8_t)(s_heartbeat >> 8);
    buf[2] = (uint8_t)(s_heartbeat & 0xFFu);
    return 3u;
}

/* ---- HAL callbacks (USART1 is the only UART in use) ----------------------- */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    wioe5_port_stm32_rx_cplt(&s_wio_hw, huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    wioe5_port_stm32_error(&s_wio_hw, huart);
}

/* ---- Idle ------------------------------------------------------------------ */

static void idle_mcu(void)
{
    /* TODO(stop2): when an RTC wakeup timer exists and wioe5_can_stop(&s_wio)
     * is true, program the wakeup for the remaining wait, enter Stop 2, then
     * SystemClock_Config() and wioe5_port_stm32_resume(&s_wio_hw). */
    __WFI();
}

/* ---- Public ---------------------------------------------------------------- */

void app_node_init(UART_HandleTypeDef *huart)
{
    wioe5_config_t    cfg;
    app_lorawan_cfg_t app_cfg;

    memset(&s_wio_hw, 0, sizeof(s_wio_hw));
    s_wio_hw.huart    = huart;
    s_wio_hw.rst_port = reset_wioe5_GPIO_Port;
    s_wio_hw.rst_pin  = reset_wioe5_Pin;

    wioe5_config_default(&cfg); /* AU915, sub-band 2, OTAA, ADR on, port 1 */
    cfg.on_event    = on_event;
    cfg.on_downlink = on_downlink;
    cfg.user        = &s_app;

    wioe5_port_t port = wioe5_port_stm32(&s_wio_hw, &s_wio);
    if (wioe5_init(&s_wio, &port, &cfg) != WIOE5_OK) {
        Error_Handler();
    }

    app_lorawan_default_cfg(&app_cfg);
    app_lorawan_init(&s_app, &s_wio, &app_cfg, clock_ms, NULL, heartbeat_payload, NULL);

    if (wioe5_port_stm32_start(&s_wio_hw) != HAL_OK) {
        Error_Handler();
    }
}

void app_node_process(void)
{
    app_lorawan_process(&s_app);
    if (app_lorawan_can_idle(&s_app)) {
        idle_mcu();
    }
}
