#include "lvww_private.h"

typedef struct
{
    lvww_city_t city;
    const char *search_terms;
} lvww_city_catalog_item_t;

static const lvww_city_catalog_item_t lvww_city_catalog[] = {
    {{"beijing", "北京", "北京市", "中国", "Asia/Shanghai", 39.9042, 116.4074},
     "beijing 北京 北京市"},
    {{"shanghai", "上海", "上海市", "中国", "Asia/Shanghai", 31.2304, 121.4737},
     "shanghai 上海 上海市"},
    {{"shenzhen", "深圳", "广东省", "中国", "Asia/Shanghai", 22.5431, 114.0579},
     "shenzhen 深圳 广东 广东省"},
    {{"guangzhou", "广州", "广东省", "中国", "Asia/Shanghai", 23.1291, 113.2644},
     "guangzhou 广州 广东 广东省"},
    {{"chengdu", "成都", "四川省", "中国", "Asia/Shanghai", 30.5728, 104.0668},
     "chengdu 成都 四川 四川省"},
    {{"hangzhou", "杭州", "浙江省", "中国", "Asia/Shanghai", 30.2741, 120.1551},
     "hangzhou 杭州 浙江 浙江省"},
    {{"wuhan", "武汉", "湖北省", "中国", "Asia/Shanghai", 30.5928, 114.3055},
     "wuhan 武汉 湖北 湖北省"},
    {{"xian", "西安", "陕西省", "中国", "Asia/Shanghai", 34.3416, 108.9398},
     "xian xi'an 西安 陕西 陕西省"}
};

static int lvww_ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static rt_bool_t lvww_text_contains_ci(const char *text, const char *query)
{
    rt_size_t text_len;
    rt_size_t query_len;
    rt_size_t i;
    rt_size_t j;

    if (!text || !query)
        return RT_FALSE;
    query_len = rt_strlen(query);
    if (!query_len)
        return RT_TRUE;
    text_len = rt_strlen(text);
    if (query_len > text_len)
        return RT_FALSE;
    for (i = 0; i + query_len <= text_len; ++i)
    {
        for (j = 0; j < query_len; ++j)
        {
            if (lvww_ascii_lower((unsigned char)text[i + j]) !=
                lvww_ascii_lower((unsigned char)query[j]))
                break;
        }
        if (j == query_len)
            return RT_TRUE;
    }
    return RT_FALSE;
}

rt_size_t lvww_city_catalog_search(const char *query,
                                   lvww_city_t *cities,
                                   rt_size_t capacity)
{
    rt_size_t i;
    rt_size_t count = 0;

    if (!cities || !capacity)
        return 0;
    if (!query)
        query = "";
    for (i = 0; i < sizeof(lvww_city_catalog) / sizeof(lvww_city_catalog[0]) &&
                count < capacity; ++i)
    {
        if (!query[0] ||
            lvww_text_contains_ci(lvww_city_catalog[i].search_terms, query))
            cities[count++] = lvww_city_catalog[i].city;
    }
    return count;
}
