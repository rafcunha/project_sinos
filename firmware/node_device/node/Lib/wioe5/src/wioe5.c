/* STUB: interface only. Implementation pending (development phase). */
#include "wioe5.h"

#include <string.h>

void wioe5_config_default(wioe5_config_t *cfg)
{
    if (cfg != NULL) {
        memset(cfg, 0, sizeof(*cfg));
    }
}

wioe5_status_t wioe5_init(wioe5_t *dev, const wioe5_port_t *port, const wioe5_config_t *cfg)
{
    (void)dev; (void)port; (void)cfg;
    return WIOE5_ERR_STATE;
}

void wioe5_process(wioe5_t *dev)
{
    (void)dev;
}

void wioe5_rx_isr(wioe5_t *dev, uint8_t byte)
{
    (void)dev; (void)byte;
}

wioe5_status_t wioe5_start_setup(wioe5_t *dev)
{
    (void)dev;
    return WIOE5_ERR_STATE;
}

wioe5_status_t wioe5_start_join(wioe5_t *dev, bool force)
{
    (void)dev; (void)force;
    return WIOE5_ERR_STATE;
}

wioe5_status_t wioe5_start_send(wioe5_t *dev, uint8_t fport, const uint8_t *data, size_t len,
                                bool confirmed)
{
    (void)dev; (void)fport; (void)data; (void)len; (void)confirmed;
    return WIOE5_ERR_STATE;
}

wioe5_status_t wioe5_start_sleep(wioe5_t *dev)
{
    (void)dev;
    return WIOE5_ERR_STATE;
}

wioe5_status_t wioe5_start_wake(wioe5_t *dev)
{
    (void)dev;
    return WIOE5_ERR_STATE;
}

wioe5_status_t wioe5_start_reset(wioe5_t *dev)
{
    (void)dev;
    return WIOE5_ERR_STATE;
}

bool wioe5_is_busy(const wioe5_t *dev)
{
    (void)dev;
    return false;
}

bool wioe5_can_stop(const wioe5_t *dev)
{
    (void)dev;
    return false;
}

wioe5_status_t wioe5_last_result(const wioe5_t *dev)
{
    (void)dev;
    return WIOE5_ERR_STATE;
}

const wioe5_info_t *wioe5_info(const wioe5_t *dev)
{
    return (dev != NULL) ? &dev->info : NULL;
}

wioe5_status_t wioe5_wait(wioe5_t *dev, void (*idle_hook)(void))
{
    (void)dev; (void)idle_hook;
    return WIOE5_ERR_STATE;
}
