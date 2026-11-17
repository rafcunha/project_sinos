/**
 * @file fake_port.h
 * @brief Test double for wioe5_port_t: controllable clock, captured TX,
 *        recorded reset pin and log lines.
 */
#ifndef FAKE_PORT_H
#define FAKE_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wioe5.h"

#define FAKE_TX_CAP    16384u
#define FAKE_RESET_LOG 16u
#define FAKE_LOG_CAP   16384u

typedef struct {
    uint32_t t;
    bool     asserted;
} fake_reset_ev_t;

typedef struct {
    uint32_t        now;
    uint8_t         tx[FAKE_TX_CAP];
    size_t          tx_len;
    int             write_result; /* value returned by write() */
    uint32_t        write_calls;
    bool            has_reset_pin;
    fake_reset_ev_t reset_log[FAKE_RESET_LOG];
    size_t          reset_count;
    char            log[FAKE_LOG_CAP];
    size_t          log_len;
} fake_port_t;

void         fake_port_init(fake_port_t *fp, bool with_reset_pin);
wioe5_port_t fake_port_iface(fake_port_t *fp);
bool         fake_tx_contains(const fake_port_t *fp, const char *s);
bool         fake_log_contains(const fake_port_t *fp, const char *s);

#endif /* FAKE_PORT_H */
