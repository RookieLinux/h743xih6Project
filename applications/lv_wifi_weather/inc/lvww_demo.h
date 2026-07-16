#ifndef LVWW_DEMO_H
#define LVWW_DEMO_H

#include "lvww_mock.h"

#ifdef __cplusplus
extern "C" {
#endif

int lvww_demo_start(lv_obj_t *parent);
void lvww_demo_stop(void);
lvww_mock_t *lvww_demo_mock(void);

#ifdef __cplusplus
}
#endif

#endif
