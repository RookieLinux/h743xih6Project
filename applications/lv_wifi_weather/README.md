# LVGL 8.3.11 Wi-Fi 天气终端

这是一个面向 **RT-Thread + LVGL 8.3.11** 的 800×480 横屏触摸组件。核心只依赖
RT-Thread 与 LVGL，不直接依赖 WLAN、HTTP、JSON、FlashDB、NTP 或特定天气服务。

组件提供：

- 中文深色首页、城市当地时间、当前天气与今日摘要；
- Wi-Fi 扫描、多账号保存、编辑、删除、连接、断开和最近成功账号自动重连；
- 共用的 LVGL 屏幕键盘、密码隐藏/显示和 WPA 密码格式检查；
- 英文/拼音城市搜索、500 ms 防抖、城市时区和天气缓存；
- 异步请求 ID、迟到结果过滤、RT-Thread 消息队列和 UI 线程事件消费；
- 可运行的异步模拟后端。

## 目录

```text
lv_wifi_weather/
├── inc/
│   ├── lv_wifi_weather.h   # 公共数据结构、端口契约和组件 API
│   ├── lvww_mock.h         # 模拟后端 API
│   └── lvww_demo.h         # 演示入口
├── src/                    # UI、状态机、缓存和事件队列
├── ports/mock/             # 独立工作线程模拟后端
├── examples/               # 最小启动示例
├── Kconfig
└── SConscript
```

## 快速运行模拟界面

1. 将本目录复制到 RT-Thread 工程的软件包或组件目录。
2. 在工程 Kconfig 中 source 本组件的 `Kconfig`，在上层 `SConscript` 中包含本目录。
3. 启用 `PKG_USING_LV_WIFI_WEATHER`、`LVWW_USING_MOCK` 和 `LVWW_BUILD_DEMO`。
4. 确认 LVGL 已完成 800×480 显示、触摸输入和 tick/handler 初始化。
5. 在 LVGL 线程创建页面：

```c
#include "lvww_demo.h"

void application_ui_create(void)
{
    lvww_demo_start(lv_scr_act());
}
```

模拟热点中加密网络接受任意合法密码；输入 `wrongpass` 可触发认证失败。城市搜索支持
Beijing、Shanghai、Shenzhen、Guangzhou、Chengdu、Hangzhou、London 和 New York。

可从测试控制台或测试代码切换错误状态：

```c
lvww_mock_set_network_available(lvww_demo_mock(), RT_FALSE); /* 模拟掉线 */
lvww_mock_set_storage_failure(lvww_demo_mock(), RT_TRUE);    /* 模拟 KV 写失败 */
```

结束时应先解除后端绑定，再销毁 UI；`lvww_demo_stop()` 已按正确顺序处理。

## 接入真实后端

实现一个静态 `lvww_port_ops_t`，再将它与后端上下文传给 `lvww_create()`：

```c
static const lvww_port_ops_t board_ops = {
    .wifi_scan      = board_wifi_scan,
    .wifi_connect   = board_wifi_connect,
    .wifi_disconnect= board_wifi_disconnect,
    .city_search    = board_city_search,
    .weather_fetch  = board_weather_fetch,
    .time_sync      = board_time_sync,
    .cancel         = board_cancel,
    .kv_read        = board_kv_read,
    .kv_write       = board_kv_write,
    .kv_delete      = board_kv_delete,
};

lvww_config_t config;
lvww_config_init(&config);
lvww_ctx_t *ui = lvww_create(lv_scr_act(), &config, &board_ops, &board_context);
```

所有网络入口必须快速返回：`RT_EOK` 表示请求已被后端接受，不表示操作已经成功。网络、
DNS、TLS、JSON 解析和 NTP 必须在后端线程完成。完成后构造 `lvww_event_t` 并调用：

```c
lvww_event_t event = {0};
event.type = LVWW_EVT_TIME_RESULT;
event.request_id = request_id;
event.data.time.utc_epoch = utc_epoch;
lvww_post_event(ui, &event);
```

`lvww_post_event()` 会复制整个事件，可以从任意 RT-Thread 线程调用。后端不得直接调用
LVGL。每个结果必须带回原始请求 ID；`request_id == 0` 只用于 Wi-Fi 驱动主动上报的掉线、
重连等状态。

销毁组件前，应用应阻止新的后端事件。`lvww_destroy()` 会调用活动请求的 `cancel()`，但
后端仍须保证在取消完成后不再使用已经销毁的 `lvww_ctx_t *`。推荐生命周期顺序为：

1. 解除后端与 UI 上下文的绑定；
2. 调用 `lvww_destroy()`，让组件向仍然有效的后端发送取消请求；
3. 停止或排空后端工作队列；
4. 释放后端资源。

## 端口数据约定

### Wi-Fi

- SSID 最长 32 字节，组件最多接收 20 个扫描结果、保存 8 个账号。
- `wifi_connect()` 的凭据只在调用期间借用；后端如需异步使用必须复制。
- 开放网络的 `secure` 为 0、密码为空；企业 Wi-Fi、Portal 与 WPS 不在组件范围内。
- 连接成功或失败均回传 `LVWW_EVT_WIFI_STATE`。认证失败使用
  `LVWW_WIFI_REASON_AUTH`，以便 UI 给出明确提示。

### 城市与天气

- `city_search()` 接收 ASCII 英文名或拼音，返回最多 8 个 `lvww_city_t`。
- 城市名称应优先返回 Latin/拼音形式；若传入完整 CJK 字体，也可直接返回 UTF-8 中文名。
- `weather_fetch()` 返回标准化 `lvww_weather_t`，温度单位为摄氏度、风速为 km/h。
- `utc_offset_seconds` 必须是观测时间对应的实际偏移，包含夏令时；组件不会修改系统全局时区。
- 推荐后端使用 Open-Meteo Geocoding 搜索城市，并将 Forecast API 当前值、当日最高/最低
  和 `utc_offset_seconds` 映射到结构体。核心不关心具体供应商，也不解析 JSON。

### UTC 校时

- `time_sync()` 成功后返回 Unix UTC 秒数；推荐真实后端通过 SNTP 获取。
- 组件用 RT-Thread tick 推进时间，首次联网立即校时，之后默认每 6 小时重新同步。

### KV 存储

- `kv_read()` 的 `*length` 是缓冲区容量，成功时改为实际长度。
- `kv_write()` 的 `LVWW_KV_FLAG_SECRET` 表示数据包含 Wi-Fi 密码。产品端应将其映射到加密
  分区、安全存储或设备唯一密钥加密；模拟后端只保存在 RAM。
- 组件数据带 magic、版本和 CRC。损坏或版本不兼容的数据会被忽略。

## 字体与 LVGL 配置

中文固定文案优先使用 LVGL 8.3 自带的 `lv_font_simsun_16_cjk` 子集。建议在
`lv_conf.h` 中启用：

```c
#define LV_FONT_SIMSUN_16_CJK 1
#define LV_FONT_MONTSERRAT_48 1
```

如果未启用，组件仍可编译并回退到 `LV_FONT_DEFAULT`，但默认字体可能无法显示中文。
也可通过 `lvww_config_t.font_ui/font_large/font_city` 注入项目自己的字体。任意中文城市名
通常需要更完整的 CJK 字库；默认模拟数据使用 Latin 名称以控制 Flash 占用。

建议同时确认：

```c
#define LV_USE_FLEX       1
#define LV_USE_KEYBOARD   1
#define LV_USE_TEXTAREA   1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX   1
#define LV_USE_LABEL      1
```

## 默认调度与限制

- 天气每 30 分钟刷新；UTC 每 6 小时同步。均可在 `lvww_config_t` 修改。
- 断网不会清空天气缓存；首页显示“离线缓存”。
- 每次定时器最多消费 4 个后端事件，避免一次性阻塞 LVGL 帧循环。
- 组件固定为横屏布局，默认根对象尺寸为 800×480；显示与触摸驱动由 BSP 提供。
- 组件不创建网络线程，只有可选模拟后端创建一个工作线程。
