#include "lvww_demo.h"

typedef struct
{
    lvww_ctx_t *ui;
    lvww_mock_t *mock;
} lvww_demo_t;

static lvww_demo_t demo;

int lvww_demo_start(lv_obj_t *parent)
{
    lvww_config_t config;
    if (!parent || demo.ui)
        return -RT_EINVAL;

    demo.mock = lvww_mock_create();
    if (!demo.mock)
        return -RT_ENOMEM;

    lvww_config_init(&config);
    demo.ui = lvww_create(parent, &config, lvww_mock_get_ops(), demo.mock);
    if (!demo.ui)
    {
        lvww_mock_destroy(demo.mock);
        demo.mock = RT_NULL;
        return -RT_ENOMEM;
    }
    lvww_mock_bind(demo.mock, demo.ui);
    return RT_EOK;
}

void lvww_demo_stop(void)
{
    if (!demo.ui)
        return;
    lvww_mock_bind(demo.mock, RT_NULL);
    lvww_destroy(demo.ui);
    lvww_mock_destroy(demo.mock);
    demo.ui = RT_NULL;
    demo.mock = RT_NULL;
}

lvww_mock_t *lvww_demo_mock(void)
{
    return demo.mock;
}
