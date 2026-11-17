#include "modem_sim.h"

#include <stdio.h>
#include <string.h>

static void escape(const uint8_t *s, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 5u < cap; i++) {
        uint8_t c = s[i];
        if (c == '\r') {
            out[o++] = '\\';
            out[o++] = 'r';
        } else if (c == '\n') {
            out[o++] = '\\';
            out[o++] = 'n';
        } else if (c < 0x20u || c >= 0x7Fu) {
            o += (size_t)snprintf(&out[o], cap - o, "\\x%02X", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static void fail(modem_sim_t *sim, const char *what, const uint8_t *got, size_t got_len)
{
    char esc[256];
    if (sim->failed) {
        return;
    }
    sim->failed = true;
    escape(got, got_len, esc, sizeof(esc));
    snprintf(sim->fail_msg, sizeof(sim->fail_msg), "t=%lu step %u: %s; driver sent \"%s\"",
             (unsigned long)sim->fp->now, (unsigned)sim->next, what, esc);
}

void sim_init(modem_sim_t *sim, fake_port_t *fp, wioe5_t *dev)
{
    memset(sim, 0, sizeof(*sim));
    sim->fp          = fp;
    sim->dev         = dev;
    sim->tx_consumed = fp->tx_len;
}

void sim_add(modem_sim_t *sim, const sim_step_t *steps, size_t n)
{
    for (size_t i = 0; i < n && sim->n_steps < SIM_MAX_STEPS; i++) {
        sim->steps[sim->n_steps++] = steps[i];
    }
}

static bool replies_pending(const modem_sim_t *sim)
{
    return sim->active != NULL && sim->reply_idx < SIM_MAX_REPLIES &&
           sim->active->reply[sim->reply_idx].text != NULL;
}

static bool spontaneous_waiting(const modem_sim_t *sim)
{
    return sim->next < sim->n_steps && sim->steps[sim->next].expect == NULL;
}

static void activate(modem_sim_t *sim)
{
    const sim_step_t *st       = &sim->steps[sim->next];
    sim->match_time[sim->next] = sim->fp->now;
    sim->active                = st;
    sim->reply_idx             = 0;
    sim->reply_due             = sim->fp->now + st->reply[0].delay_ms;
    sim->next++;
}

static void deliver_due(modem_sim_t *sim)
{
    while (replies_pending(sim) && (int32_t)(sim->fp->now - sim->reply_due) >= 0) {
        const sim_reply_t *r = &sim->active->reply[sim->reply_idx];
        size_t             n = strlen(r->text);
        for (size_t i = 0; i < n; i++) {
            wioe5_rx_isr(sim->dev, (uint8_t)r->text[i]);
        }
        if (!r->raw) {
            wioe5_rx_isr(sim->dev, '\r');
            wioe5_rx_isr(sim->dev, '\n');
        }
        sim->last_reply_time = sim->fp->now;
        sim->reply_idx++;
        if (replies_pending(sim)) {
            sim->reply_due = sim->fp->now + sim->active->reply[sim->reply_idx].delay_ms;
        }
    }
}

static void check_tx(modem_sim_t *sim)
{
    for (;;) {
        size_t avail = sim->fp->tx_len - sim->tx_consumed;
        if (avail == 0u || sim->failed) {
            return;
        }
        const uint8_t *got = &sim->fp->tx[sim->tx_consumed];
        if (sim->next >= sim->n_steps) {
            fail(sim, "unexpected TX after end of script", got, avail);
            return;
        }
        const sim_step_t *st = &sim->steps[sim->next];
        if (st->expect == NULL) {
            fail(sim, "TX while a spontaneous step was pending", got, avail);
            return;
        }
        size_t n = strlen(st->expect);
        size_t k = avail < n ? avail : n;
        if (memcmp(got, st->expect, k) != 0) {
            char want[256];
            char msg[400];
            escape((const uint8_t *)st->expect, n, want, sizeof(want));
            snprintf(msg, sizeof(msg), "expected \"%s\"", want);
            fail(sim, msg, got, avail);
            return;
        }
        if (avail < n) {
            return; /* partial command so far */
        }
        if (replies_pending(sim)) {
            fail(sim, "new command before previous replies were delivered", got, n);
            return;
        }
        sim->tx_consumed += n;
        activate(sim);
    }
}

void sim_tick(modem_sim_t *sim)
{
    if (!replies_pending(sim) && spontaneous_waiting(sim)) {
        activate(sim);
    }
    deliver_due(sim);
    if (sim->process != NULL) {
        sim->process(sim->process_ctx);
    } else {
        wioe5_process(sim->dev);
    }
    check_tx(sim);
    sim->fp->now++;
}

void sim_run_until_time(modem_sim_t *sim, uint32_t t_end)
{
    while ((int32_t)(sim->fp->now - t_end) < 0) {
        sim_tick(sim);
    }
}

uint32_t sim_run_until_idle(modem_sim_t *sim, uint32_t max_ms)
{
    uint32_t start = sim->fp->now;
    do {
        sim_tick(sim);
    } while ((sim->fp->now - start) < max_ms &&
             (wioe5_is_busy(sim->dev) || replies_pending(sim) || spontaneous_waiting(sim)));
    return sim->fp->now - start;
}

bool sim_done(const modem_sim_t *sim)
{
    return !sim->failed && sim->next == sim->n_steps && !replies_pending(sim) &&
           sim->tx_consumed == sim->fp->tx_len;
}

const char *sim_status(modem_sim_t *sim)
{
    if (sim->failed) {
        return sim->fail_msg;
    }
    if (sim->next < sim->n_steps) {
        char        want[256];
        const char *e = sim->steps[sim->next].expect;
        if (e == NULL) {
            e = "(spontaneous)";
        }
        escape((const uint8_t *)e, strlen(e), want, sizeof(want));
        snprintf(sim->fail_msg, sizeof(sim->fail_msg),
                 "script stopped at step %u, still waiting for \"%s\"", (unsigned)sim->next, want);
        return sim->fail_msg;
    }
    if (replies_pending(sim)) {
        return "replies still pending";
    }
    if (sim->tx_consumed != sim->fp->tx_len) {
        fail(sim, "unmatched TX at end of script", &sim->fp->tx[sim->tx_consumed],
             sim->fp->tx_len - sim->tx_consumed);
        return sim->fail_msg;
    }
    return "ok";
}
