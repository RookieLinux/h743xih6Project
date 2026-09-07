# MQTT 批量 OTA 方案与 JSON 报文框架

## 1. 总体架构

批量升级使用 MQTT 作为控制面，固件使用 HTTP/HTTPS 作为数据面：

```text
PC
├─ MQTT Broker
├─ OTA 管理服务
│  ├─ 维护 hardware_id 对应的最新固件版本
│  ├─ 响应设备版本查询
│  └─ 汇总设备升级进度和结果
└─ HTTP 固件服务
   └─ 提供相同 .fwpkg 给多台设备下载

设备
├─ 上电连接 MQTT
├─ 请求最新版本
├─ 比较 version_code
├─ 在 LVGL 首页提示用户
└─ 用户确认后通过 HTTP 下载
   └─ ota_begin/write/finish → upgrade → 重启 → Bootloader 安装
```

MQTT Broker 本身不负责保存或传输大体积固件。PC 除 Broker 外还应运行一个
HTTP 文件服务或 OTA 管理服务的下载接口。这样大量设备可以共享同一个固件
URL，MQTT 中只传递 JSON 元数据、控制事件和进度。

现有 TCP `5000` 推送服务可以继续保留，作为单机维护和调试通道；批量升级的
默认路径改为 MQTT 查询/上报加 HTTP 主动下载。

设备端现已实现 MQTT 控制器、cJSON 严格解析、HTTP 流式下载以及 UI 状态接入。
协议编解码仍独立放在 `ota_mqtt_protocol.h/.c`，服务端或其他传输适配器可以
直接复用 DTO 和 JSON 接口，不需要依赖 uMQTT、WebClient 或 Flash。

## 2. 设备启动流程

1. 生成稳定且唯一的 `device_id`，MQTT Client ID 同样使用该值。
2. 连接 Broker，配置 Last Will，并订阅本设备的 `version/response`。
3. 订阅成功后发布 `version/request`，QoS 使用 1。
4. 收到响应后检查：
   - `protocol` 必须为 `ota-v1`；
   - `request_message_id` 必须匹配本次请求；
   - `hardware_id` 必须与设备一致；
   - `version_code` 必须大于本地版本；
   - 包大小不能超过 1 MiB OTA 分区；
   - URL、CRC32 和 SHA-256 字段完整。
5. 若有新版本，通过 `lvww_set_firmware_info()` 将版本和更新说明投递到 UI。
6. 用户点击“立即更新”后，UI 回调只向 OTA 工作线程投递任务，不在 LVGL
   线程中进行 MQTT、HTTP 或 Flash 操作。
7. 工作线程等待 `0..rollout_delay_max_seconds` 的随机时间，再开始下载，
   避免大量设备同时占满 AP 和 PC 网络。
8. HTTP 数据顺序调用 `ota_begin("http", ...)`、`ota_write()` 和
   `ota_finish()`。
9. 各阶段通过 MQTT 上报 `update/event`。提交成功后复位，由 Bootloader
   安装。
10. 新 APP 启动后再次查询版本，并上报 `completed`。

## 3. MQTT Topics

版本请求、响应和阶段事件使用 QoS 1；高频 `download_progress` 使用 QoS 0，
避免等待 PUBACK 阻塞 HTTP 数据流。状态主题使用 retained，普通请求、响应和
事件不 retained。

| 方向 | Topic | 用途 |
|---|---|---|
| 设备 → 服务 | `ota/v1/device/{device_id}/version/request` | 上电查询最新版本 |
| 服务 → 设备 | `ota/v1/device/{device_id}/version/response` | 返回固件元数据 |
| 设备 → 服务 | `ota/v1/device/{device_id}/update/event` | 用户决定、进度和结果 |
| 设备 → 服务 | `ota/v1/device/{device_id}/status` | 在线状态和 Last Will |

服务端订阅：

```text
ota/v1/device/+/version/request
ota/v1/device/+/update/event
```

## 4. JSON 报文

### 4.1 上电版本查询

Topic：

```text
ota/v1/device/H743-001122334455/version/request
```

Payload：

```json
{
  "protocol": "ota-v1",
  "type": "version_request",
  "message_id": "H743-001122334455-42",
  "device_id": "H743-001122334455",
  "hardware_id": 1211577395,
  "current_version": "V1.0.0",
  "current_version_code": 65536,
  "bootloader_version": "V1.0.0"
}
```

`hardware_id` 的十六进制值为 `0x48373433`。`message_id` 必须在设备重启和
MQTT QoS 1 重发场景下可用于去重。

### 4.2 服务端版本响应

Topic：

```text
ota/v1/device/H743-001122334455/version/response
```

有更新时：

```json
{
  "protocol": "ota-v1",
  "type": "version_response",
  "message_id": "server-20260730-10001",
  "request_message_id": "H743-001122334455-42",
  "device_id": "H743-001122334455",
  "hardware_id": 1211577395,
  "update_available": true,
  "mandatory": false,
  "rollout_delay_max_seconds": 300,
  "firmware": {
    "version": "V1.1.0",
    "version_code": 65792,
    "package_size": 812544,
    "package_crc32": "7A4E21C9",
    "package_sha256": "64_HEX_CHARACTERS",
    "download_url": "http://192.168.1.10:8000/firmware/h743/V1.1.0.fwpkg",
    "release_notes": "提升网络稳定性，新增批量 OTA，并修复界面问题。"
  }
}
```

无更新时：

```json
{
  "protocol": "ota-v1",
  "type": "version_response",
  "message_id": "server-20260730-10002",
  "request_message_id": "H743-001122334455-42",
  "device_id": "H743-001122334455",
  "hardware_id": 1211577395,
  "update_available": false,
  "firmware": {
    "version": "V1.0.0",
    "version_code": 65536
  }
}
```

设备以 `version_code` 作比较，`version` 只用于显示。服务端不得仅依赖字符串
版本排序。

### 4.3 用户确认与升级进度

用户点击确认：

```json
{
  "protocol": "ota-v1",
  "type": "update_event",
  "message_id": "H743-001122334455-43",
  "device_id": "H743-001122334455",
  "firmware_message_id": "server-20260730-10001",
  "event": "accepted",
  "version_code": 65792
}
```

下载进度：

```json
{
  "protocol": "ota-v1",
  "type": "update_event",
  "message_id": "H743-001122334455-51",
  "device_id": "H743-001122334455",
  "firmware_message_id": "server-20260730-10001",
  "event": "download_progress",
  "version_code": 65792,
  "progress_percent": 46,
  "received_size": 373760,
  "package_size": 812544
}
```

失败：

```json
{
  "protocol": "ota-v1",
  "type": "update_event",
  "message_id": "H743-001122334455-56",
  "device_id": "H743-001122334455",
  "firmware_message_id": "server-20260730-10001",
  "event": "failed",
  "version_code": 65792,
  "stage": "verify",
  "error_code": -9,
  "error_message": "payload SHA-256 mismatch"
}
```

`event` 可取：

```text
accepted
declined
download_started
download_progress
verifying
committed
rebooting
completed
failed
```

进度不应按每个数据块都发布。建议每增加 5% 或每隔 5 秒发布一次。

### 4.4 在线状态与 Last Will

在线 retained 消息：

```json
{
  "protocol": "ota-v1",
  "type": "device_status",
  "device_id": "H743-001122334455",
  "online": true,
  "current_version": "V1.0.0",
  "current_version_code": 65536
}
```

连接 MQTT 时预先配置相同 Topic 的 retained Last Will：

```json
{
  "protocol": "ota-v1",
  "type": "device_status",
  "device_id": "H743-001122334455",
  "online": false
}
```

## 5. 批量调度约束

- 服务端按 `hardware_id`、设备组和灰度比例决定返回哪个版本。
- 首批选择少量设备，确认 `completed` 后再扩大批次。
- `rollout_delay_max_seconds` 用于设备侧随机错峰，不代表用户确认超时。
- 同一 `firmware_message_id` 重复到达时不得重复创建下载任务。
- 下载失败可指数退避重试，校验失败不得自动无限重试。
- `mandatory` 当前只影响 UI 提示，不绕过用户确认。
- 固件数字签名是批量生产环境的首要安全要求；MQTT 用户名密码或 TLS 不能
  替代 Bootloader 对固件签名的验证。
