#include "fake_port.h"

#include <string.h>

static int fake_write(void *ctx, const uint8_t *data, size_t len)
{
    fake_port_t *fp = (fake_port_t *)ctx;
    fp->write_calls++;
    if (fp->write_result != 0) {
        return fp->write_result;
    }
    if (fp->tx_len + len <= FAKE_TX_CAP) {
        memcpy(&fp->tx[fp->tx_len], data, len);
        fp->tx_len += len;
    }
    return 0;
}

static uint32_t fake_now(void *ctx)
{
    return ((fake_port_t *)ctx)->now;
}

static void fake_set_reset(void *ctx, bool asserted)
{
    fake_port_t *fp = (fake_port_t *)ctx;
    if (fp->reset_count < FAKE_RESET_LOG) {
        fp->reset_log[fp->reset_count].t        = fp->now;
        fp->reset_log[fp->reset_count].asserted = asserted;
        fp->reset_count++;
    }
}

static void fake_log(void *ctx, wioe5_log_level_t level, const char *msg)
{
    fake_port_t *fp = (fake_port_t *)ctx;
    size_t       n  = strlen(msg);
    (void)level;
    if (fp->log_len + n + 2u <= FAKE_LOG_CAP) {
        memcpy(&fp->log[fp->log_len], msg, n);
        fp->log_len += n;
        fp->log[fp->log_len++] = '\n';
        fp->log[fp->log_len]   = '\0';
    }
}

void fake_port_init(fake_port_t *fp, bool with_reset_pin)
{
    memset(fp, 0, sizeof(*fp));
    fp->now           = 1000u; /* non-zero start catches "0 means unset" bugs */
    fp->has_reset_pin = with_reset_pin;
}

wioe5_port_t fake_port_iface(fake_port_t *fp)
{
    wioe5_port_t p;
    memset(&p, 0, sizeof(p));
    p.write     = fake_write;
    p.now_ms    = fake_now;
    p.set_reset = fp->has_reset_pin ? fake_set_reset : NULL;
    p.log       = fake_log;
    p.ctx       = fp;
    return p;
}

static bool mem_contains(const uint8_t *hay, size_t hay_len, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0u) {
        return true;
    }
    for (size_t i = 0; i + n <= hay_len; i++) {
        if (memcmp(&hay[i], needle, n) == 0) {
            return true;
        }
    }
    return false;
}

bool fake_tx_contains(const fake_port_t *fp, const char *s)
{
    return mem_contains(fp->tx, fp->tx_len, s);
}

bool fake_log_contains(const fake_port_t *fp, const char *s)
{
    return mem_contains((const uint8_t *)fp->log, fp->log_len, s);
}
