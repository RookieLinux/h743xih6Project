#include "lvww_rw007_demo.h"

typedef struct
{
    lvww_ctx_t *ui;
    lvww_rw007_t *backend;
} lvww_rw007_demo_t;

static lvww_rw007_demo_t demo;

int lvww_rw007_demo_start(lv_obj_t *parent)
{
    lvww_config_t config;
    if (!parent || demo.ui)
        return -RT_EINVAL;

    demo.backend = lvww_rw007_create();
    if (!demo.backend)
        return -RT_ENOMEM;

    lvww_config_init(&config);
    demo.ui = lvww_create(parent, &config, lvww_rw007_get_ops(), demo.backend);
    if (!demo.ui)
    {
        lvww_rw007_destroy(demo.backend);
        demo.backend = RT_NULL;
        return -RT_ENOMEM;
    }
    lvww_rw007_bind(demo.backend, demo.ui);
    return RT_EOK;
}

void lvww_rw007_demo_stop(void)
{
    if (!demo.ui)
        return;
    lvww_rw007_bind(demo.backend, RT_NULL);
    lvww_destroy(demo.ui);
    lvww_rw007_destroy(demo.backend);
    demo.ui = RT_NULL;
    demo.backend = RT_NULL;
}
