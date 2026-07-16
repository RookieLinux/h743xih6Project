#ifndef LVWW_RW007_H
#define LVWW_RW007_H

#include "lv_wifi_weather.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lvww_rw007 lvww_rw007_t;

lvww_rw007_t *lvww_rw007_create(void);
void lvww_rw007_bind(lvww_rw007_t *backend, lvww_ctx_t *ctx);
const lvww_port_ops_t *lvww_rw007_get_ops(void);
void lvww_rw007_destroy(lvww_rw007_t *backend);

#ifdef __cplusplus
}
#endif

#endif
