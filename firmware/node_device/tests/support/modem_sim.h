/**
 * @file modem_sim.h
 * @brief Scripted Wio-E5 simulator driving a wioe5_t through a fake_port_t.
 *
 * A script is a list of steps. Each step waits for the driver to transmit
 * exactly @c expect (or starts as soon as it is reached when @c expect is
 * NULL) and then delivers its replies. Reply delays are cumulative: each one
 * counts from the previous reply (the first one from the moment the step
 * matched). Text replies get "\r\n" appended; raw replies go byte for byte.
 *
 * The simulator fails (sim->failed, sim->fail_msg) when the driver sends
 * something other than the next expected command, or sends a new command
 * while replies of the previous step are still pending.
 *
 * One tick = deliver due replies, wioe5_process(), check TX, clock += 1 ms.
 */
#ifndef MODEM_SIM_H
#define MODEM_SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fake_port.h"
#include "wioe5.h"

#define SIM_MAX_REPLIES 12u
#define SIM_MAX_STEPS   48u

typedef struct {
    uint32_t    delay_ms;
    const char *text;
    bool        raw;
} sim_reply_t;

typedef struct {
    const char *expect;
    sim_reply_t reply[SIM_MAX_REPLIES];
} sim_step_t;

typedef struct {
    fake_port_t      *fp;
    wioe5_t          *dev;
    sim_step_t        steps[SIM_MAX_STEPS];
    size_t            n_steps;
    size_t            next;        /* next step to match              */
    size_t            tx_consumed; /* bytes of fp->tx already matched */
    const sim_step_t *active;      /* step whose replies are pending  */
    size_t            reply_idx;
    uint32_t          reply_due;
    uint32_t          match_time[SIM_MAX_STEPS];
    uint32_t          last_reply_time;
    bool              failed;
    char              fail_msg[512];
} modem_sim_t;

void sim_init(modem_sim_t *sim, fake_port_t *fp, wioe5_t *dev);
/** Append steps to the script (copied). */
void sim_add(modem_sim_t *sim, const sim_step_t *steps, size_t n);
void sim_tick(modem_sim_t *sim);
/** Tick while fp->now < t_end. */
void sim_run_until_time(modem_sim_t *sim, uint32_t t_end);
/** Tick at least once, then until the driver is idle, no reply is pending and
 *  no spontaneous step is waiting (or @p max_ms elapse). @return elapsed ms. */
uint32_t sim_run_until_idle(modem_sim_t *sim, uint32_t max_ms);
/** All steps matched, all replies delivered, no unmatched TX, no failure. */
bool sim_done(const modem_sim_t *sim);
/** Human-readable reason when sim_done() is false. */
const char *sim_status(modem_sim_t *sim);

#define SIM_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#endif /* MODEM_SIM_H */
