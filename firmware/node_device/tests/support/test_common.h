/**
 * @file test_common.h
 * @brief Shared test rig: driver + fake port + simulator + callback capture,
 *        and canned scripts that bring the driver to a known state.
 */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fake_port.h"
#include "modem_sim.h"
#include "wioe5.h"

/* ---- Callback capture ------------------------------------------------- */

#define CAP_MAX_EVENTS 32

typedef struct {
    int            seq;            /* increments on every callback          */
    int            op_done_count;
    wioe5_op_t     last_op;
    wioe5_status_t last_status;
    uint32_t       op_done_at;     /* fake clock at last on_op_done          */
    int            op_done_seq;
    int            dl_count;
    int            dl_seq;
    uint8_t        dl_data[WIOE5_MAX_DOWNLINK];
    size_t         dl_len;
    uint8_t        dl_port;
    int16_t        dl_rssi;
    int16_t        dl_snr;
    uint8_t        dl_window;
    int            n_events;
    wioe5_event_t  events[CAP_MAX_EVENTS];
} cb_capture_t;

extern cb_capture_t g_cb;

void cb_reset(void);
int  cb_event_count(wioe5_event_t evt);

/* ---- Rig --------------------------------------------------------------- */

typedef struct {
    fake_port_t    fp;
    wioe5_config_t cfg;
    wioe5_t        dev;
    modem_sim_t    sim;
} rig_t;

/** The rig the callbacks read the clock from (set by rig_init). */
extern rig_t *g_rig;

/** Default config + capture callbacks. Does not call wioe5_init. */
void rig_prepare(rig_t *r, bool reset_pin);
/** rig_prepare + wioe5_init + fresh simulator. @return wioe5_init status. */
wioe5_status_t rig_init(rig_t *r, bool reset_pin);
/** Re-create the simulator with an empty script (keeps TX history). */
void rig_new_script(rig_t *r);
/** Add steps and run until idle. */
void rig_run(rig_t *r, const sim_step_t *steps, size_t n, uint32_t max_ms);

/** Runs the default setup script (default config). @return true if setup OK. */
bool rig_setup(rig_t *r);
/** rig_setup + successful join. */
bool rig_setup_join(rig_t *r);
/** rig_setup_join + module put to sleep. */
bool rig_setup_join_sleep(rig_t *r);

/** Transmission time the driver adds to a command timeout: ceil(len * 1.1) ms. */
uint32_t tx_ms(size_t len);

/* ---- Canned scripts ------------------------------------------------------ */

/** Setup with the default config and no reset pin. */
extern const sim_step_t SCRIPT_SETUP[];
extern const size_t     SCRIPT_SETUP_N;
/** Same, without the first step (the probe "AT"). */
#define SCRIPT_SETUP_AFTER_PROBE (&SCRIPT_SETUP[1])
#define SCRIPT_SETUP_AFTER_PROBE_N (SCRIPT_SETUP_N - 1u)

extern const sim_step_t SCRIPT_JOIN_OK[];
extern const size_t     SCRIPT_JOIN_OK_N;
extern const sim_step_t SCRIPT_SLEEP_OK[];
extern const size_t     SCRIPT_SLEEP_OK_N;
/** Wake byte + probe answered OK. */
extern const sim_step_t SCRIPT_WAKE_OK[];
extern const size_t     SCRIPT_WAKE_OK_N;

#define DEVEUI_TEXT "2C:F7:F1:20:42:00:12:34"

#endif /* TEST_COMMON_H */
