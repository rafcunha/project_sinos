/**
 * @file app_node.c
 * @brief STM32 glue for the node: driver instance, HAL callbacks, uplink payload
 *        (app_payload, docs/payload/payload_spec.md) and idle policy.
 *
 * Low power (driver spec 10.7 and 18): when the cycle only waits, the Wio-E5
 * sleeps (wioe5_can_stop()) and the wait is at least STOP2_MIN_IDLE_MS, the MCU
 * enters Stop 2 and the RTC wakeup timer (1 s resolution, LSE) brings it back.
 * Shorter waits, or waits with the module awake, use Sleep mode (__WFI).
 * HAL_GetTick() stops in Stop 2, so the slept time, measured on the RTC, is
 * added to the HAL tick on wake-up; the cycle timing stays correct.
 */
#include "app_node.h"

#include <string.h>

#include "app_lorawan.h"
#include "app_payload.h"
#include "main.h"
#include "wioe5.h"
#include "wioe5_port_stm32.h"

/** Below this wait, Stop 2 entry/exit overhead is not worth it. */
#define STOP2_MIN_IDLE_MS 1500u
/** RTC wakeup counter is 16 bits at 1 Hz (ck_spre): up to 65536 s. */
#define STOP2_MAX_S 65536u

extern RTC_HandleTypeDef hrtc;
void                     SystemClock_Config(void);

/* ---- Firmware identification ------------------------------------------------ */

#ifndef FW_VERSION
#define FW_VERSION "0.0.0-dev"
#endif
#ifndef FW_GIT_SHA
#define FW_GIT_SHA "unknown"
#endif

/** Kept in the image (linker --undefined) so the version can be read from the
 *  .bin (`strings node.bin | grep project_sinos`) or with the debugger. */
const char g_fw_ident[] = "project_sinos node " FW_VERSION "+" FW_GIT_SHA;

/* ---- Instances ----------------------------------------------------------- */

static wioe5_t       s_wio;
static wioe5_stm32_t s_wio_hw;
static app_lorawan_t s_app;
static app_payload_t s_payload;

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

/** Milliseconds since midnight from the RTC calendar (sub-second resolution). */
static uint32_t rtc_ms_of_day(void)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    if (HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) {
        return 0u;
    }
    (void)HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN); /* unlocks the shadow registers */
    uint32_t sub_ms = ((t.SecondFraction - t.SubSeconds) * 1000u) / (t.SecondFraction + 1u);
    return (((uint32_t)t.Hours * 60u + t.Minutes) * 60u + t.Seconds) * 1000u + sub_ms;
}

static void enter_stop2(uint32_t seconds)
{
    uint32_t t0 = rtc_ms_of_day();
    if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, seconds - 1u, RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0u) !=
        HAL_OK) {
        __WFI();
        return;
    }

    HAL_SuspendTick();
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    /* Woken up (RTC wakeup is the only enabled Stop 2 wakeup source). */
    SystemClock_Config(); /* back from MSI to the configured clock tree */
    (void)HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);

    uint32_t t1    = rtc_ms_of_day();
    uint32_t slept = (t1 >= t0) ? (t1 - t0) : (t1 + 86400000u - t0);
    __disable_irq();
    uwTick += slept; /* keep HAL_GetTick() consistent with real time */
    __enable_irq();
    HAL_ResumeTick();

    wioe5_port_stm32_resume(&s_wio_hw);
}

static void idle_mcu(void)
{
    uint32_t idle = app_lorawan_idle_ms(&s_app);
    if (wioe5_can_stop(&s_wio) && (idle >= STOP2_MIN_IDLE_MS)) {
        uint32_t seconds = idle / 1000u; /* round down: the remainder runs in Sleep */
        if (seconds > STOP2_MAX_S) {
            seconds = STOP2_MAX_S;
        }
        enter_stop2(seconds);
    } else {
        __WFI();
    }
}

/* ---- Public ---------------------------------------------------------------- */

void app_node_init(UART_HandleTypeDef *huart)
{
    wioe5_config_t    cfg;
    app_lorawan_cfg_t app_cfg;

#ifdef DEBUG
    /* Keep the debugger attached across Stop 2 (costs current: Debug builds only). */
    HAL_DBGMCU_EnableDBGStopMode();
#endif

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

    /* Until the sensor driver exists: status at boot and every 24 h, heartbeat
     * otherwise. reset_cause is read from RCC with the watchdog work (6.5). */
    app_payload_init(&s_payload, FW_VERSION, NODE_PAYLOAD_RESET_UNKNOWN, &s_app, HAL_GetTick());

    app_lorawan_default_cfg(&app_cfg);
    app_cfg.fport = NODE_PAYLOAD_FPORT;
    app_lorawan_init(&s_app, &s_wio, &app_cfg, clock_ms, NULL, app_payload_build, &s_payload);

    if (wioe5_port_stm32_start(&s_wio_hw) != HAL_OK) {
        Error_Handler();
    }
}

void app_node_process(void)
{
    app_payload_tick(&s_payload, HAL_GetTick()); /* 64-bit uptime for the status message */
    app_lorawan_process(&s_app);
    if (app_lorawan_can_idle(&s_app)) {
        idle_mcu();
    }
}
