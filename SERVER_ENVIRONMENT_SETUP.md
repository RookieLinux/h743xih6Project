# Caddy 与 EMQX 服务环境搭建（Windows / Ubuntu）

[中文](SERVER_ENVIRONMENT_SETUP.md) | [English](SERVER_ENVIRONMENT_SETUP_EN.md)

本文用于搭建本工程 OTA/联网功能所需的主机侧服务：

- Caddy：监听 TCP `8000`，提供固件等静态文件下载，并允许浏览目录。
- EMQX：提供 MQTT Broker；设备连接端口为 `1883`，管理面板端口为 `18083`。

服务配置、网站文件和运行数据均放在工程外的独立目录。本工程只保存这份搭建文档和固件源码；构建完成后，将发布用的 `.fwpkg` 上传到 Caddy 服务器。

除固件构建、上传外，安装和管理命令均在对应的 Windows 或 Ubuntu 服务器上执行，不需要进入工程目录，也不需要在服务器上克隆工程。Caddy 与 EMQX 可以部署在同一台主机，也可以分别部署到两台远端主机。

文中的 `<Caddy地址>`、`<EMQX地址>` 分别表示设备可访问的服务 IP 或域名，可以是局域网地址、VPN 地址或远端地址。`localhost` 和 `127.0.0.1` 仅用于在对应服务器本机测试，不能作为开发板访问远端服务的地址。

## 1. 目录规划

以下为独立部署目录示例，可按服务器实际磁盘调整；调整后应同步修改配置中的绝对路径：

| 内容 | Windows 服务器 | Ubuntu 服务器 |
| --- | --- | --- |
| Caddy 配置文件 | `D:\Services\Caddy\Caddyfile` | `/etc/caddy/Caddyfile` |
| Caddy 网站根目录 | `D:\Services\Caddy\www` | `/srv/caddy/www` |
| EMQX Compose 文件 | `D:\Services\EMQX\compose.yaml` | `/opt/emqx-deploy/compose.yaml` |
| EMQX Docker 数据、日志 | Docker 管理的命名卷 | Docker 管理的命名卷 |

Windows 没有 D 盘时可改为 `C:\Services`。Caddy 网站根目录中只存放待发布文件，配置文件与 EMQX 数据均位于网站根目录之外。网站目录结构为：

```text
www/
└── firmware/
    └── h743/
        └── V1.1.0.fwpkg
```

创建目录：

Windows PowerShell：

```powershell
New-Item -ItemType Directory -Force 'D:\Services\Caddy\www\firmware\h743'
New-Item -ItemType Directory -Force 'D:\Services\EMQX'
```

Ubuntu：

```bash
sudo mkdir -p /srv/caddy/www/firmware/h743
sudo mkdir -p /opt/emqx-deploy
```

将待下载的 `.fwpkg` 固件复制或上传到 Caddy 服务器网站根目录下的 `firmware/h743/`。对应的设备下载地址示例为：

```text
http://<Caddy地址>:8000/firmware/h743/V1.1.0.fwpkg
```

## 2. 安装与配置 Caddy

### 2.1 Windows 安装

任选一种方式安装：

```powershell
# Scoop（社区维护）
scoop install caddy

# 或 Chocolatey（社区维护，管理员 PowerShell）
choco install caddy
```

也可以从 [Caddy 官方下载页](https://caddyserver.com/download) 下载 Windows 可执行文件，并将 `caddy.exe` 所在目录加入 `PATH`。

检查安装：

```powershell
caddy version
```

### 2.2 Ubuntu 安装

使用 Caddy 官方软件源安装稳定版：

```bash
sudo apt install -y debian-keyring debian-archive-keyring apt-transport-https curl
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo chmod o+r /usr/share/keyrings/caddy-stable-archive-keyring.gpg
sudo chmod o+r /etc/apt/sources.list.d/caddy-stable.list
sudo apt update
sudo apt install -y caddy
```

安装软件包后会创建并启动 `caddy` systemd 服务。检查安装：

```bash
caddy version
systemctl status caddy --no-pager
```

### 2.3 Caddy 配置

Windows：新建 `D:\Services\Caddy\Caddyfile`（无 `.txt` 后缀），内容如下。Caddyfile 中的 Windows 路径使用正斜杠：

```caddyfile
:8000 {
    root * D:/Services/Caddy/www
    file_server browse
    log {
        output stdout
        format console
    }
}
```

配置含义：

- `:8000`：在所有网络接口上监听 HTTP 8000 端口。
- `root * D:/Services/Caddy/www`：使用服务器上的绝对网站路径，不依赖启动命令所在目录。
- `file_server browse`：开启静态文件服务；目录中不存在 `index.html` 或 `index.txt` 时显示目录列表。
- `log`：把访问日志输出到终端；Ubuntu systemd 会自动收集这些日志。

Ubuntu：在 `/etc/caddy/Caddyfile` 中使用相同配置，将 `root` 一行替换为 `root * /srv/caddy/www`。若文件已经有站点配置，应先备份并保留仍需使用的站点。

> `browse` 会把网站根目录中的文件名暴露给访问者。不要把密钥、账号、源码或其他敏感文件放进网站目录，生产环境建议限制防火墙来源或关闭目录浏览。

先校验配置：

```powershell
caddy validate --config 'D:\Services\Caddy\Caddyfile'
```

```bash
sudo caddy validate --config /etc/caddy/Caddyfile
```

#### Windows 启动

在 Windows 服务器上打开 PowerShell，运行：

```powershell
caddy run --config 'D:\Services\Caddy\Caddyfile'
```

需要停止时按 `Ctrl+C`。Caddy 官方不建议在 Windows 上依赖 `caddy start` 后台运行，因为关闭启动它的终端也会终止进程；需要长期运行时应按 Caddy 官方说明注册为 Windows 服务。

#### Ubuntu 前台启动（开发/调试）

前台调试与 systemd 运行方式二选一。软件包安装后已自动启动服务，若需要前台调试，应先停止服务，避免端口和管理接口冲突：

```bash
sudo systemctl stop caddy
sudo -u caddy caddy run --config /etc/caddy/Caddyfile
```

#### Ubuntu systemd 启动（长期部署）

网站文件直接部署到 `/srv/caddy/www`，无需复制工程目录。安装 Caddy 后，为该独立网站目录设置服务账号的读取和目录遍历权限：

```bash
sudo chown -R caddy:caddy /srv/caddy/www
sudo chmod -R u+rX /srv/caddy/www
```

将 `/etc/caddy/Caddyfile` 设置为：

```caddyfile
:8000 {
    root * /srv/caddy/www
    file_server browse
    log {
        output stdout
        format console
    }
}
```

如果之前在前台调试，先按 `Ctrl+C` 退出。校验后启动服务并平滑加载配置：

```bash
sudo caddy validate --config /etc/caddy/Caddyfile
sudo systemctl enable --now caddy
sudo systemctl reload caddy
systemctl status caddy --no-pager
```

以后替换固件时，将文件上传到 `/srv/caddy/www/firmware/h743/`，并确保 `caddy` 用户有读取权限；仅更新静态文件不需要重启 Caddy。

### 2.4 验证 Caddy

在服务器本机访问：

```text
http://localhost:8000/
```

在能连接 Caddy 服务器的开发电脑或其他设备上访问：

```text
http://<Caddy地址>:8000/
```

看到目录列表，并能下载 `firmware/h743/` 中的固件，即表示配置成功。也可以执行：

```powershell
curl.exe -I http://localhost:8000/firmware/h743/V1.1.0.fwpkg
```

```bash
curl -I http://localhost:8000/firmware/h743/V1.1.0.fwpkg
```

### 2.5 Caddy 日常运行操作

Caddy 没有类似 EMQX Dashboard 的管理网页，也没有“登录 Caddy 后台”这一步。它通过 Caddyfile、命令行和操作系统服务进行管理。

Windows 前台运行方式：

| 操作 | PowerShell 命令 |
| --- | --- |
| 校验配置 | `caddy validate --config 'D:\Services\Caddy\Caddyfile'` |
| 启动并查看实时日志 | `caddy run --config 'D:\Services\Caddy\Caddyfile'` |
| 平滑加载修改后的配置 | `caddy reload --config 'D:\Services\Caddy\Caddyfile'` |
| 查看进程 | `Get-Process caddy -ErrorAction SilentlyContinue` |
| 停止前台进程 | 在运行窗口按 `Ctrl+C` |

`caddy reload` 要在同一台 Caddy 服务器上的另一个 PowerShell 窗口执行；使用绝对路径后，窗口当前目录不限。若已经把 Caddy 注册为 Windows 服务，启停、开机自启和日志位置应以所使用的服务管理器配置为准，配置文件参数也应使用绝对路径。

Ubuntu systemd 方式：

| 操作 | 命令 |
| --- | --- |
| 启动 | `sudo systemctl start caddy` |
| 停止 | `sudo systemctl stop caddy` |
| 重启 | `sudo systemctl restart caddy` |
| 开机自启 | `sudo systemctl enable caddy` |
| 取消开机自启 | `sudo systemctl disable caddy` |
| 查看状态 | `systemctl status caddy --no-pager` |
| 校验并平滑加载配置 | `sudo caddy validate --config /etc/caddy/Caddyfile && sudo systemctl reload caddy` |
| 查看最近 100 行日志 | `journalctl -u caddy -n 100 --no-pager` |
| 持续查看日志 | `journalctl -u caddy -f` |

修改 Caddyfile 后应先执行校验，再使用 `reload` 平滑加载；仅替换网站根目录中的静态文件时不需要重载。

### 2.6 可选：为 Caddy 页面增加登录密码

如果目录浏览页面只允许授权人员访问，可以开启 HTTP Basic Authentication。先生成密码哈希，命令会交互式要求输入密码，不会把明文写进终端历史：

```text
caddy hash-password
```

复制输出的哈希值，然后把服务器上的 Caddyfile 改为以下形式，把 `<生成的密码哈希>` 整体替换为真实值。以下为 Windows 示例；Ubuntu 将 `root` 改为 `/srv/caddy/www`：

```caddyfile
:8000 {
    root * D:/Services/Caddy/www

    basic_auth {
        ota_admin <生成的密码哈希>
    }

    file_server browse
    log {
        output stdout
        format console
    }
}
```

校验并重载后，浏览器访问 `http://<Caddy地址>:8000/` 会弹出登录框，用户名为 `ota_admin`，密码为生成哈希时输入的原始密码。

> Basic Auth 在纯 HTTP 上不能保护密码传输安全，只适合可信局域网临时使用。更重要的是，这个配置会同时保护固件下载 URL；如果当前设备 OTA 客户端没有发送 Basic Auth 凭据，下载会收到 `401 Unauthorized`。因此本工程默认配置不启用 Caddy 登录认证。需要公网部署时应先配置 HTTPS，并同步实现设备端认证。

## 3. 安装与部署 EMQX

### 3.1 推荐方案：Docker Compose（Windows / Ubuntu 通用）

Windows 安装并启动 Docker Desktop（使用 WSL 2 后端）；Ubuntu 安装 Docker Engine 与 Compose 插件。确认下面两条命令可用：

```text
docker --version
docker compose version
```

在 EMQX 服务器独立目录中新建 Compose 文件：Windows 为 `D:\Services\EMQX\compose.yaml`，Ubuntu 为 `/opt/emqx-deploy/compose.yaml`。Ubuntu 可使用 `sudoedit /opt/emqx-deploy/compose.yaml` 编辑。内容如下：

```yaml
services:
  emqx:
    image: emqx/emqx:6.3.0
    container_name: h743-emqx
    hostname: node1.emqx.local
    restart: unless-stopped
    environment:
      EMQX_NODE_NAME: emqx@node1.emqx.local
    ports:
      - "1883:1883"   # MQTT/TCP
      - "8083:8083"   # MQTT/WebSocket
      - "8883:8883"   # MQTT/TLS（使用前需正确配置证书）
      - "8084:8084"   # MQTT/WSS（使用前需正确配置证书）
      - "18083:18083" # Dashboard 与 REST API
    volumes:
      - emqx_data:/opt/emqx/data
      - emqx_log:/opt/emqx/log
    healthcheck:
      test: ["CMD", "/opt/emqx/bin/emqx", "ctl", "status"]
      interval: 10s
      timeout: 5s
      retries: 12

volumes:
  emqx_data:
  emqx_log:
```

示例固定镜像标签为 `6.3.0`，实际部署后需按下文验证运行状态。升级前应阅读 EMQX 发行说明，并先备份数据卷。命名卷由服务器上的 Docker 管理，不存放在固件工程中；把 Compose 文件复制到另一台服务器不会自动迁移这些数据。

在 EMQX 服务器上进入部署目录：

Windows PowerShell：

```powershell
Set-Location 'D:\Services\EMQX'
```

Ubuntu：

```bash
cd /opt/emqx-deploy
```

启动并检查状态：

```text
docker compose up -d
docker compose ps
docker compose logs --tail 100 emqx
```

停止但保留数据：

```text
docker compose down
```

不要使用 `docker compose down -v`，该命令会删除 EMQX 的持久化数据卷。

Docker Compose 日常运行操作：

| 操作 | 命令 |
| --- | --- |
| 首次创建并启动 | `docker compose up -d` |
| 启动已有容器 | `docker compose start` |
| 停止但保留容器和数据 | `docker compose stop` |
| 重启 | `docker compose restart emqx` |
| 查看状态 | `docker compose ps` |
| 检查 EMQX 节点 | `docker exec h743-emqx emqx ctl status` |
| 查看最近 100 行日志 | `docker compose logs --tail 100 emqx` |
| 持续查看日志 | `docker compose logs -f emqx` |
| 删除容器但保留命名卷 | `docker compose down` |

执行这些命令时应位于保存 `compose.yaml` 的目录。日志跟随模式按 `Ctrl+C` 只会退出日志查看，不会停止 EMQX。

### 3.2 Ubuntu 原生安装（可选）

Ubuntu 22.04/24.04 可按 EMQX 官方文档使用 APT 安装：

```bash
curl -s https://packagecloud.io/install/repositories/emqx/emqx-enterprise5/script.deb.sh | sudo bash
sudo apt-get install -y emqx
sudo systemctl enable --now emqx
systemctl status emqx --no-pager
```

Ubuntu 原生安装的日常操作：

| 操作 | 命令 |
| --- | --- |
| 启动 | `sudo systemctl start emqx` |
| 停止 | `sudo systemctl stop emqx` |
| 重启 | `sudo systemctl restart emqx` |
| 开机自启 | `sudo systemctl enable emqx` |
| 查看状态 | `systemctl status emqx --no-pager` |
| 检查节点 | `sudo emqx ctl status` |
| 查看最近 100 行日志 | `journalctl -u emqx -n 100 --no-pager` |
| 持续查看日志 | `journalctl -u emqx -f` |

原生安装的常用目录为：

| 内容 | 路径 |
| --- | --- |
| 配置 | `/etc/emqx` |
| 数据 | `/var/lib/emqx` |
| 日志 | `/var/log/emqx` |

Windows 不在当前 EMQX 原生安装包的官方支持列表中，因此 Windows 请使用 Docker Desktop/WSL 2 方案，不建议依赖旧版 Windows ZIP 包。

### 3.3 EMQX 端口

| 端口 | 用途 | 本工程是否必需 |
| --- | --- | --- |
| `1883/TCP` | MQTT 明文连接 | 是（开发板默认连接方式） |
| `8883/TCP` | MQTT TLS | 按需，生产环境推荐 |
| `8083/TCP` | MQTT over WebSocket | 按需 |
| `8084/TCP` | MQTT over WSS | 按需 |
| `18083/TCP` | Dashboard / REST API | 管理时需要 |

EMQX 还会使用节点发现/集群内部端口。单机部署时不要把这些内部端口映射到公网。

### 3.4 登录与基本配置

浏览器打开：

```text
http://localhost:18083/
```

从开发电脑访问 EMQX 管理面板时使用：

```text
http://<EMQX地址>:18083/
```

首次登录默认账号通常为 `admin`、密码为 `public`，登录后会要求修改密码。应立即设置强密码。然后在 Dashboard 中完成以下操作：

1. 在 **Management → Listeners** 确认 TCP 监听器正在 `1883` 端口运行。
2. 在 **Access Control → Authentication** 创建适合本工程的认证方式与 MQTT 用户。
3. 将 MQTT 用户名和密码配置到设备端，生产环境不要允许匿名客户端。

设备端 Broker 地址应填写：

```text
<EMQX地址>:1883
```

Dashboard 登录与退出：

1. 打开 `http://<EMQX地址>:18083/`。
2. 首次使用输入用户名 `admin`、密码 `public`。
3. 按页面提示设置新的强密码并妥善保存。
4. 后续仍使用用户名 `admin` 和新密码登录。
5. 操作完成后，点击页面右上角用户菜单中的退出选项；不要在公共计算机上保存密码。

如果忘记 Dashboard 管理员密码，可以在服务器终端重置。Docker 部署：

```text
docker exec h743-emqx emqx ctl admins passwd admin '<新密码>'
```

Ubuntu 原生部署：

```bash
sudo emqx ctl admins passwd admin '<新密码>'
```

命令成功返回 `ok` 后，用新密码重新登录。密码可能会保留在终端历史中，生产环境应在执行后按系统安全规范清理相关历史，或使用专门创建的临时管理员账号完成恢复。

### 3.5 MQTT 收发验证

可以使用跨平台 MQTTX，或 Ubuntu 的 Mosquitto 客户端验证。Ubuntu 安装测试客户端：

```bash
sudo apt install -y mosquitto-clients
```

终端 1 订阅：

```bash
mosquitto_sub -h localhost -p 1883 -t 'ota/v1/test/#' -v
```

终端 2 发布：

```bash
mosquitto_pub -h localhost -p 1883 -t 'ota/v1/test/ping' -m '{"message":"hello"}'
```

如果已在 EMQX 中启用认证，请为两个命令补充 `-u '<用户名>' -P '<密码>'`。订阅端收到消息即表示 Broker 工作正常。

从开发电脑测试远端 EMQX 时，把测试命令中的 `localhost` 改为 `<EMQX地址>`。Dashboard 管理员账号与 MQTT 客户端账号分别管理，不能默认混用。

## 4. 防火墙与远端访问

在各自服务器上只开放实际需要的端口。两台主机分开部署时，Caddy 主机开放 `8000`，EMQX 主机按需开放 `1883` 和 `18083`。Windows 请在管理员 PowerShell 中执行适用于该主机的规则：

```powershell
New-NetFirewallRule -DisplayName "H743 Caddy HTTP 8000" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8000 -Profile Private
New-NetFirewallRule -DisplayName "H743 EMQX MQTT 1883" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 1883 -Profile Private
New-NetFirewallRule -DisplayName "H743 EMQX Dashboard 18083" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 18083 -Profile Private
```

Ubuntu 使用 UFW 时：

```bash
sudo ufw allow 8000/tcp
sudo ufw allow 1883/tcp
sudo ufw allow 18083/tcp
sudo ufw status
```

建议把 `18083` 仅开放给管理网段，不要直接暴露到公网。公网或生产环境还应启用 MQTT TLS、客户端认证、主题授权，并为 Caddy 配置 HTTPS 或置于可信反向代理之后。

远端服务器还需要检查云安全组、VPN 路由或路由器端口转发。Windows 示例规则适用于 Private 网络配置文件，其他部署环境应按实际网络配置文件设置。开发板必须同时能访问 Caddy 下载地址和 EMQX Broker 地址；两者不要求位于同一局域网。

## 5. 远端部署与本工程 OTA 配合

### 5.1 发布固件到独立服务器

1. 在开发电脑构建、打包并校验 `.fwpkg`。
2. 通过远程桌面文件传输、SFTP 或已有发布工具，把包上传到 Caddy 服务器。Windows 目标为 `D:\Services\Caddy\www\firmware\h743\`，Ubuntu 目标为 `/srv/caddy/www/firmware/h743/`。
3. 若上传账号没有网站目录写权限，先上传到该账号的临时目录，再由服务器管理员复制到目标路径并设置 Caddy 读取权限。
4. 上传完成后再发布版本元数据，避免设备下载到尚未传完的包。使用新的版本文件名，并通过实际下载 URL 核对大小和哈希。

Caddy 提供静态下载服务，本文配置不提供浏览器上传功能。服务器不需要安装本工程的交叉编译工具链。EMQX 负责 MQTT 消息转发；自动响应设备版本查询还需要配套 OTA 管理服务，安装 Broker 本身不会生成版本响应。

### 5.2 分别设置服务地址

设备 MQTT 配置指向 `<EMQX地址>:1883`，版本响应的固件 URL 指向 `<Caddy地址>:8000`。例如 Caddy 和 EMQX 分别位于两台服务器时，固件 URL 使用 Caddy 的地址，不使用 Broker 地址。

服务端返回给设备的版本响应可使用如下地址：

```json
{
  "download_url": "http://<Caddy地址>:8000/firmware/h743/V1.1.0.fwpkg"
}
```

完整 MQTT Topic 和 JSON 报文约定见 [`applications/ota/MQTT_BATCH_OTA.md`](applications/ota/MQTT_BATCH_OTA.md)。部署完成后至少检查：

- 开发板能访问 `<Caddy地址>:8000` 并下载完整固件。
- 开发板能连接 `<EMQX地址>:1883`。
- 固件 URL 中没有使用 `localhost`。
- `.fwpkg` 的文件大小、CRC32 和 SHA-256 与版本响应一致。
- 多设备升级时已配置 `rollout_delay_max_seconds`，避免同时下载。

## 6. 常见问题

### 8000 端口无法访问

- 用 `caddy validate` 检查 Caddyfile。
- 确认 Caddy 加载的是服务器上的配置文件，且 `root` 绝对路径对应实际网站目录。
- 检查 `8000` 是否被其他程序占用，以及系统防火墙是否放行。
- 在 Ubuntu systemd 模式下确认 `/srv/caddy/www` 可被 `caddy` 用户读取。

### 浏览器没有显示目录列表

`file_server browse` 只会在目录中没有 `index.html` 或 `index.txt` 时显示目录列表。删除/改名索引文件，或直接访问包含固件的子目录。

### EMQX Dashboard 可打开，但开发板无法连接

- Dashboard 的 `18083` 与 MQTT 的 `1883` 是不同端口。
- 检查 TCP Listener 的绑定地址和运行状态。
- 检查 Docker 端口映射、防火墙、用户名密码和主题授权。
- 确认开发板与 EMQX 服务器路由互通，并使用设备可访问的 EMQX IP 或域名。

### 重启 EMQX 容器后配置丢失

确认 Compose 中保留了 `/opt/emqx/data` 的命名卷，并且停止时没有执行 `docker compose down -v`。同时不要随意修改 `EMQX_NODE_NAME`，EMQX 数据目录与节点名关联。

## 7. 官方资料

- [Caddy 安装](https://caddyserver.com/docs/install)
- [Caddy `file_server` 与 `browse`](https://caddyserver.com/docs/caddyfile/directives/file_server)
- [Caddy 命令行与平滑重载](https://caddyserver.com/docs/command-line)
- [Caddy `basic_auth`](https://caddyserver.com/docs/caddyfile/directives/basic_auth)
- [EMQX Docker 安装](https://docs.emqx.com/en/emqx/latest/deploy/install-docker.html)
- [EMQX Ubuntu 安装](https://docs.emqx.com/en/emqx/latest/deploy/install-ubuntu.html)
- [EMQX Listener 配置与默认端口](https://docs.emqx.com/en/emqx/latest/configuration/listener.html)
- [EMQX 管理命令](https://docs.emqx.com/en/emqx/latest/admin/cli.html)
