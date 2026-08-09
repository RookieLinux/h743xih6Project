# APP OTA 设计与 Wi-Fi 上位机接口

## 1. OTA 数据流

```text
上位机 .fwpkg
    │  Wi-Fi / TCP :5000
    ▼
download 分区（先写正文，最后写包头）
    │  整包 CRC + 包头 CRC + payload CRC32/SHA-256/签名策略
    ▼
upgrade 分区（先擦除并复制 payload，复验后最后写包头）
    │  APP 主动复位
    ▼
Bootloader 校验 upgrade → 安装到内部 app 分区
```

`download`、`upgrade` 都使用
`h743xih6Bootloader/bootloader/boot_image.h` 定义的固件包。APP 中的
`boot_image.h` 是该线格式的镜像定义；Bootloader 修改格式时必须同步修改并
重新联编两边。

断电安全策略：

- 下载开始时擦除 `download` 所需扇区，传输期间把前 256 字节包头只保存在
  RAM，正文直接写 Flash。
- 整包接收完成并通过全部校验后，才把包头写入 `download`。
- `upgrade` 先擦除、复制 payload 并复验，最后才写入包头。
- 因此断电最多留下无有效包头的半包，Bootloader 不会把它识别成升级镜像。
- 若 `upgrade` 包头已经提交，Bootloader 可在下次上电继续安装；内部 APP
  安装失败仍由现有 factory 回退逻辑处理。

## 2. 可扩展传输接口

核心入口在 `ota.h`，与 Wi-Fi 无关：

```c
ota_begin("usb", package_size, package_crc32);
ota_write(offset, data, length);  /* 必须从 0 开始顺序写入 */
ota_finish();                     /* 校验 download 并原子提交 upgrade */
ota_reboot_to_install(500);
```

以后增加 UART/YMODEM、USB CDC、U 盘、HTTP/HTTPS 拉取、BLE 等方式时，只需
实现“获得总长度和整包 CRC → 顺序调用 begin/write/finish”的 transport
适配器，不应自行操作 `download`/`upgrade` 分区。

APP 与 Bootloader 现在使用同一个 ECDSA P-256 公钥进行双重验签。签名为
64 字节大端 `r || s`，覆盖域分隔符、硬件 ID、固件版本、Payload 大小、加载
地址、CRC32 和 SHA-256 等不可变元数据。默认拒绝 `BOOT_SIGNATURE_NONE`；
`OTA_ALLOW_UNSIGNED_IMAGES=1` 只能用于从旧版无签名系统迁移，不能用于量产。

现有设备首次迁移时，旧 APP/Bootloader 尚不能识别签名包，需要把包含本验签
实现的新 APP 用 `mkimage.py --unsigned` 打成唯一一次过渡包。过渡 APP 启动后
默认立即进入严格模式，后续只接受签名包；再烧写签名 factory 包和严格模式
Bootloader。不要把 `--unsigned` 继续用于日常发布。

首次开发可运行 `tools/ota_signing_key.py` 生成本机私钥和 APP 工程中的
`applications/ota/ota_trusted_key.h`。脚本不会修改 Bootloader 工程；确认密钥
后，需要手动把该头文件复制到 Bootloader 的 `bootloader/ota_trusted_key.h`。
私钥路径已加入 `.gitignore`，不得提交到仓库；量产必须替换为离线或 HSM 托管
的生产密钥。

## 3. Wi-Fi TCP 协议 v1

设备作为 TCP Server，连接现有 RW007 STA 网络后监听 `0.0.0.0:5000`。
上位机作为 TCP Client。所有多字节整数均为小端。

每帧固定 24 字节头：

| 偏移 | 类型 | 字段 | 说明 |
|---:|---|---|---|
| 0 | u32 | magic | `0x3141544F`，内存中为 `OTA1` |
| 4 | u16 | version | `1` |
| 6 | u16 | type | 请求类型；响应为 `type \| 0x8000` |
| 8 | u32 | sequence | 请求序号，响应原样返回 |
| 12 | u32 | offset/status | DATA 为包内偏移；响应为有符号结果码 |
| 16 | u32 | payload_length | 后续 payload 字节数，最大 4096 |
| 20 | u32 | payload_crc32 | payload 的 IEEE CRC32；空 payload 为 0 |

请求类型：

| type | 名称 | payload / 行为 |
|---:|---|---|
| 1 | QUERY | 空；响应 20 字节状态 |
| 2 | BEGIN | 12 字节：`package_size, package_crc32, flags` |
| 3 | DATA | 固件包数据；`offset` 必须等于当前已接收长度 |
| 4 | END | 空；执行完整校验并搬运至 `upgrade` |
| 5 | ABORT | 空；作废未完成的 `download` |
| 6 | REBOOT | 空；仅 READY 状态接受，复位进入 Bootloader |

BEGIN 的 `flags bit0` 表示 END 成功并回复后自动复位。QUERY 响应 payload 为：

```text
u32 state
i32 last_error
u32 package_size
u32 received_size
u32 firmware_version
```

推荐交互：

1. TCP 连接，发送 QUERY，确认设备不是忙状态。
2. 读取整个 `.fwpkg` 的文件长度和 IEEE CRC32，发送 BEGIN。
3. 收到成功响应后，每次发送不超过 4096 字节 DATA；每帧等待 ACK 后再发下一帧。
4. 发送 END。END 可能因 Flash 复验与复制耗时较长，上位机应设置至少 120 秒
   超时，并持续显示“设备校验/提交中”。
5. END 成功后按需要发送 REBOOT，或在 BEGIN 中设置自动复位标志。
6. 连接意外断开时，设备自动调用 ABORT，未完成包不会被提交。

## 4. 上位机模块规划

建议把上位机分成以下接口，GUI 不直接操作 socket：

```text
PackageService
  ├─ inspect(path): 解析 boot_image_header_t、显示硬件 ID/版本/大小
  └─ validate(path): 本地校验 header CRC、payload CRC32、SHA-256

DeviceTransport
  ├─ connect(endpoint)
  ├─ request(frame)
  └─ close()
       └─ TcpOtaTransport（当前）
          后续可加 SerialOtaTransport / UsbOtaTransport

OtaSession
  ├─ query()
  ├─ upload(path, progress_callback, cancel_token)
  ├─ abort()
  └─ reboot()
```

GUI 最少展示：设备 IP、连接状态、当前/目标版本、硬件 ID、发送字节数、阶段
（擦除/传输/校验/提交/等待重启）、设备错误码和重试入口。v1 先用手工 IP；
后续可增加 UDP 广播或 mDNS 发现，但不改变 OTA TCP 帧协议。

当前 TCP + CRC/SHA 解决的是传输完整性，不提供固件来源认证或链路机密性。
产品化时优先启用固件数字签名；如网络中有敏感信息，再增加 TLS 或在受控网关
内转发。认证、设备序列号、防降级版本策略也应作为协议 v2 的能力协商字段，
不要改变 v1 字段含义。

## 5. MQTT + HTTP 批量 OTA

远程控制器入口为 `ota_remote_start()`。公开默认配置在 `ota_config.h`。本机联调时，
复制该文件为已被 Git 忽略的 `ota_config_local.h`，再在本机文件中设置：

```c
#define OTA_MQTT_BROKER_URI  "tcp://192.168.1.10:1883"
#define OTA_MQTT_USERNAME    ""
#define OTA_MQTT_PASSWORD    ""
```

也可以构造 `ota_remote_config_t` 传给 `ota_remote_start()`，运行时指定
Broker、账号、密码、是否自动重启和是否启用批次随机延迟。默认设备 ID 使用
STM32 96-bit UID；量产序列号可通过覆盖弱函数
`ota_remote_get_device_id()` 注入。

运行期间可从首页右侧“系统更新”卡片输入新的服务器 IPv4。界面调用
`ota_remote_set_server_ip()` 后，控制器会安全断开当前 MQTT 会话，并使用
`tcp://<IPv4>:1883` 自动重连，无需重启设备。

协议层公开以下与传输无关的接口：

```c
ota_mqtt_build_version_request(...);
ota_mqtt_parse_version_response(...);
ota_mqtt_build_update_event(...);
ota_mqtt_build_device_status(...);
```

HTTP 层公开同步流式接口 `ota_http_download()`；它要求服务端返回精确的
`Content-Length`，并依次校验整包长度、整包 CRC32、整包 SHA-256、镜像头、
payload CRC/SHA 和签名策略，任何失败都会作废未完成的 download 分区。

升级后关联 `completed` 事件需要持久化
`firmware_message_id/target_version_code`。工程已在
`ota_remote_storage_ops_t` 留出 load/save/clear 回调；未接入 EasyFlash
或其他持久 KV 时，设备仍会上报 committed/rebooting，但重启后不能可靠补发
completed。

当前 WebClient 配置仍仅支持 `http://`，MQTT 也是明文 `tcp://`。固件签名已经
提供端到端来源认证，但版本元数据、账号和下载内容仍可能被旁路观察或干扰；
产品化还应启用 MQTT TLS、HTTPS、设备独立凭据和 Broker ACL。
