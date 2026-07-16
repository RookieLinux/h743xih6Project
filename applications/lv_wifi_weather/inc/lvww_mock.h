#ifndef LVWW_MOCK_H
#define LVWW_MOCK_H

#include "lv_wifi_weather.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lvww_mock lvww_mock_t;

lvww_mock_t *lvww_mock_create(void);
void lvww_mock_bind(lvww_mock_t *mock, lvww_ctx_t *ctx);
const lvww_port_ops_t *lvww_mock_get_ops(void);
void lvww_mock_destroy(lvww_mock_t *mock);

/* Use these from a console/test harness to exercise offline and error states. */
void lvww_mock_set_network_available(lvww_mock_t *mock, rt_bool_t available);
void lvww_mock_set_storage_failure(lvww_mock_t *mock, rt_bool_t fail);

#ifdef __cplusplus
}
#endif

#endif
