# Caddy and EMQX Server Setup (Windows / Ubuntu)

[中文](SERVER_ENVIRONMENT_SETUP.md) | [English](SERVER_ENVIRONMENT_SETUP_EN.md)

This guide sets up the host-side services used by this project's OTA and networking features:

- Caddy: listens on TCP port `8000`, serves firmware and other static files, and provides directory browsing.
- EMQX: provides the MQTT broker on port `1883`, with its management dashboard on port `18083`.

Service configuration, website files, and runtime data reside in independent directories outside the firmware project. The project stores this guide and the firmware source; after building, upload the release `.fwpkg` to the Caddy server.

Except for building and uploading firmware, run installation and administration commands on the corresponding Windows or Ubuntu server. There is no need to enter the project directory or clone the project on the server. Caddy and EMQX can run on the same host or on two separate remote hosts.

Throughout this guide, `<Caddy-address>` and `<EMQX-address>` mean the respective service IP address or domain name reachable by the device, whether over a LAN, VPN, or remote network. Use `localhost` and `127.0.0.1` only for tests on the corresponding server itself, never as the device's address for a remote service.

## 1. Directory Layout

The following independent deployment paths are examples. Adjust them for your server's disks and update the absolute paths in the configuration accordingly:

| Content | Windows server | Ubuntu server |
| --- | --- | --- |
| Caddy configuration | `D:\Services\Caddy\Caddyfile` | `/etc/caddy/Caddyfile` |
| Caddy website root | `D:\Services\Caddy\www` | `/srv/caddy/www` |
| EMQX Compose file | `D:\Services\EMQX\compose.yaml` | `/opt/emqx-deploy/compose.yaml` |
| EMQX Docker data and logs | Docker-managed named volumes | Docker-managed named volumes |

If Windows has no D drive, use `C:\Services` instead. Store only files intended for publication inside the Caddy website root. Configuration files and EMQX data stay outside that root. Website layout:

```text
www/
└── firmware/
    └── h743/
        └── V1.1.0.fwpkg
```

Create the directories:

Windows PowerShell:

```powershell
New-Item -ItemType Directory -Force 'D:\Services\Caddy\www\firmware\h743'
New-Item -ItemType Directory -Force 'D:\Services\EMQX'
```

Ubuntu:

```bash
sudo mkdir -p /srv/caddy/www/firmware/h743
sudo mkdir -p /opt/emqx-deploy
```

Copy or upload the `.fwpkg` firmware into `firmware/h743/` under the Caddy server's website root. Its device download URL would be:

```text
http://<Caddy-address>:8000/firmware/h743/V1.1.0.fwpkg
```

## 2. Install and Configure Caddy

### 2.1 Windows Installation

Choose one installation method:

```powershell
# Scoop (community-maintained)
scoop install caddy

# Or Chocolatey (community-maintained; administrator PowerShell)
choco install caddy
```

Alternatively, download the Windows executable from the [official Caddy download page](https://caddyserver.com/download) and add the directory containing `caddy.exe` to `PATH`.

Check the installation:

```powershell
caddy version
```

### 2.2 Ubuntu Installation

Install the stable release from the official Caddy repository:

```bash
sudo apt install -y debian-keyring debian-archive-keyring apt-transport-https curl
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo chmod o+r /usr/share/keyrings/caddy-stable-archive-keyring.gpg
sudo chmod o+r /etc/apt/sources.list.d/caddy-stable.list
sudo apt update
sudo apt install -y caddy
```

The package creates and starts the `caddy` systemd service. Check the installation:

```bash
caddy version
systemctl status caddy --no-pager
```

### 2.3 Caddy Configuration

Windows: create `D:\Services\Caddy\Caddyfile` without a `.txt` extension, using the following content. Use forward slashes for Windows paths inside the Caddyfile:

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

Configuration details:

- `:8000`: listen for HTTP on port 8000 on all network interfaces.
- `root * D:/Services/Caddy/www`: use an absolute website path on the server, independent of the command's working directory.
- `file_server browse`: serve static files and display directory listings when there is no `index.html` or `index.txt` in the requested directory.
- `log`: write access logs to the terminal; Ubuntu systemd collects these logs automatically.

Ubuntu: use the same configuration in `/etc/caddy/Caddyfile`, replacing the `root` line with `root * /srv/caddy/www`. If that file already defines sites, back it up and preserve any sites still needed.

> `browse` exposes filenames within the website root to visitors. Keep keys, credentials, source code, and other sensitive files outside the website directory. For production, restrict access by source address in the firewall or disable directory browsing.

Validate the configuration first:

```powershell
caddy validate --config 'D:\Services\Caddy\Caddyfile'
```

```bash
sudo caddy validate --config /etc/caddy/Caddyfile
```

#### Start on Windows

Open PowerShell on the Windows server and run:

```powershell
caddy run --config 'D:\Services\Caddy\Caddyfile'
```

Press `Ctrl+C` to stop. Caddy's documentation discourages relying on `caddy start` for background operation on Windows because closing the launching terminal also terminates the process. For long-running deployments, register Caddy as a Windows service using the official instructions.

#### Run in the Foreground on Ubuntu (Development/Debugging)

Choose either foreground debugging or systemd operation. Package installation already starts the service, so stop it before foreground debugging to avoid port and admin-interface conflicts:

```bash
sudo systemctl stop caddy
sudo -u caddy caddy run --config /etc/caddy/Caddyfile
```

#### Run with systemd on Ubuntu (Long-Term Deployment)

Deploy website files directly to `/srv/caddy/www`; the project directory does not need to be copied. After installing Caddy, grant its service account permission to read files and traverse directories in this independent website directory:

```bash
sudo chown -R caddy:caddy /srv/caddy/www
sudo chmod -R u+rX /srv/caddy/www
```

Set `/etc/caddy/Caddyfile` to:

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

If you were debugging in the foreground, press `Ctrl+C` first. Validate, start the service, and gracefully reload its configuration:

```bash
sudo caddy validate --config /etc/caddy/Caddyfile
sudo systemctl enable --now caddy
sudo systemctl reload caddy
systemctl status caddy --no-pager
```

For subsequent firmware releases, upload files to `/srv/caddy/www/firmware/h743/` and ensure the `caddy` user can read them. Updating static files alone does not require a Caddy restart.

### 2.4 Verify Caddy

On the server itself, visit:

```text
http://localhost:8000/
```

From a development computer or another device that can reach the Caddy server, visit:

```text
http://<Caddy-address>:8000/
```

The setup is working when you can view the directory listing and download firmware from `firmware/h743/`. You can also run:

```powershell
curl.exe -I http://localhost:8000/firmware/h743/V1.1.0.fwpkg
```

```bash
curl -I http://localhost:8000/firmware/h743/V1.1.0.fwpkg
```

### 2.5 Everyday Caddy Operations

Caddy has no built-in management webpage like EMQX Dashboard, so there is no separate Caddy dashboard login. Manage it using the Caddyfile, command line, and operating-system service manager.

Windows foreground operation:

| Operation | PowerShell command |
| --- | --- |
| Validate configuration | `caddy validate --config 'D:\Services\Caddy\Caddyfile'` |
| Start and view live logs | `caddy run --config 'D:\Services\Caddy\Caddyfile'` |
| Gracefully load updated configuration | `caddy reload --config 'D:\Services\Caddy\Caddyfile'` |
| Check processes | `Get-Process caddy -ErrorAction SilentlyContinue` |
| Stop the foreground process | Press `Ctrl+C` in its terminal |

Run `caddy reload` in another PowerShell window on the same Caddy server. Absolute paths allow either window to use any working directory. If Caddy is registered as a Windows service, use that service manager's configuration for start/stop, automatic startup, and log locations. The service's configuration-file argument should also use an absolute path.

Ubuntu systemd operation:

| Operation | Command |
| --- | --- |
| Start | `sudo systemctl start caddy` |
| Stop | `sudo systemctl stop caddy` |
| Restart | `sudo systemctl restart caddy` |
| Enable startup at boot | `sudo systemctl enable caddy` |
| Disable startup at boot | `sudo systemctl disable caddy` |
| Check status | `systemctl status caddy --no-pager` |
| Validate and gracefully reload | `sudo caddy validate --config /etc/caddy/Caddyfile && sudo systemctl reload caddy` |
| Show the last 100 log lines | `journalctl -u caddy -n 100 --no-pager` |
| Follow logs | `journalctl -u caddy -f` |

After editing the Caddyfile, validate it before using `reload`. Replacing static files in the website root does not require reloading.

### 2.6 Optional: Require a Password for Caddy Pages

To restrict directory browsing to authorized users, enable HTTP Basic Authentication. First generate a password hash. This command prompts for a password interactively rather than putting the plaintext password in shell history:

```text
caddy hash-password
```

Copy the resulting hash and update the server's Caddyfile as shown below, replacing the entire `<generated-password-hash>` placeholder. This is the Windows example; on Ubuntu, change the `root` path to `/srv/caddy/www`:

```caddyfile
:8000 {
    root * D:/Services/Caddy/www

    basic_auth {
        ota_admin <generated-password-hash>
    }

    file_server browse
    log {
        output stdout
        format console
    }
}
```

After validation and reload, visiting `http://<Caddy-address>:8000/` prompts for credentials. Use `ota_admin` and the original password entered when generating the hash.

> Basic Auth over plain HTTP does not protect credentials in transit and is suitable only for temporary use on a trusted LAN. This configuration also protects firmware download URLs. If the device's OTA client does not send Basic Auth credentials, downloads return `401 Unauthorized`. Therefore, this project's default Caddy configuration leaves authentication disabled. For public deployment, configure HTTPS and implement device-side authentication first.

## 3. Install and Deploy EMQX

### 3.1 Recommended: Docker Compose (Windows / Ubuntu)

On Windows, install and start Docker Desktop with the WSL 2 backend. On Ubuntu, install Docker Engine and the Compose plugin. Confirm both commands are available:

```text
docker --version
docker compose version
```

Create the Compose file in an independent directory on the EMQX server: `D:\Services\EMQX\compose.yaml` on Windows or `/opt/emqx-deploy/compose.yaml` on Ubuntu. On Ubuntu, you can edit it with `sudoedit /opt/emqx-deploy/compose.yaml`. Use the following content:

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
      - "8883:8883"   # MQTT/TLS (configure certificates before use)
      - "8084:8084"   # MQTT/WSS (configure certificates before use)
      - "18083:18083" # Dashboard and REST API
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

The example pins the image tag to `6.3.0`; verify its running state after deployment as described below. Read the EMQX release notes and back up the data volume before upgrading. Docker manages named volumes on the server, outside the firmware project. Copying the Compose file to another server does not automatically migrate their data.

Enter the deployment directory on the EMQX server:

Windows PowerShell:

```powershell
Set-Location 'D:\Services\EMQX'
```

Ubuntu:

```bash
cd /opt/emqx-deploy
```

Start and check status:

```text
docker compose up -d
docker compose ps
docker compose logs --tail 100 emqx
```

Stop while retaining data:

```text
docker compose down
```

Do not use `docker compose down -v`: it deletes EMQX's persistent data volumes.

Everyday Docker Compose operations:

| Operation | Command |
| --- | --- |
| Create and start for the first time | `docker compose up -d` |
| Start existing containers | `docker compose start` |
| Stop while retaining containers and data | `docker compose stop` |
| Restart | `docker compose restart emqx` |
| Check status | `docker compose ps` |
| Check the EMQX node | `docker exec h743-emqx emqx ctl status` |
| Show the last 100 log lines | `docker compose logs --tail 100 emqx` |
| Follow logs | `docker compose logs -f emqx` |
| Remove containers but retain named volumes | `docker compose down` |

Run these commands from the directory containing `compose.yaml`. Pressing `Ctrl+C` while following logs only exits the log viewer; it does not stop EMQX.

### 3.2 Native Ubuntu Installation (Optional)

On Ubuntu 22.04/24.04, use APT as described in the official EMQX documentation:

```bash
curl -s https://packagecloud.io/install/repositories/emqx/emqx-enterprise5/script.deb.sh | sudo bash
sudo apt-get install -y emqx
sudo systemctl enable --now emqx
systemctl status emqx --no-pager
```

Everyday operations for native Ubuntu installations:

| Operation | Command |
| --- | --- |
| Start | `sudo systemctl start emqx` |
| Stop | `sudo systemctl stop emqx` |
| Restart | `sudo systemctl restart emqx` |
| Enable startup at boot | `sudo systemctl enable emqx` |
| Check status | `systemctl status emqx --no-pager` |
| Check the node | `sudo emqx ctl status` |
| Show the last 100 log lines | `journalctl -u emqx -n 100 --no-pager` |
| Follow logs | `journalctl -u emqx -f` |

Common native-installation directories:

| Content | Path |
| --- | --- |
| Configuration | `/etc/emqx` |
| Data | `/var/lib/emqx` |
| Logs | `/var/log/emqx` |

Windows is not on the current official list of supported native EMQX installation platforms. Use Docker Desktop/WSL 2 on Windows instead of relying on an old Windows ZIP package.

### 3.3 EMQX Ports

| Port | Purpose | Required by this project? |
| --- | --- | --- |
| `1883/TCP` | Unencrypted MQTT | Yes (the board's default connection method) |
| `8883/TCP` | MQTT over TLS | As needed; recommended for production |
| `8083/TCP` | MQTT over WebSocket | As needed |
| `8084/TCP` | MQTT over WSS | As needed |
| `18083/TCP` | Dashboard / REST API | For administration |

EMQX also uses internal ports for node discovery and clustering. Do not publish these internal ports to the Internet for a single-node deployment.

### 3.4 Login and Basic Configuration

Open in a browser on the server:

```text
http://localhost:18083/
```

From your development computer, access the EMQX management dashboard at:

```text
http://<EMQX-address>:18083/
```

For the first login, the default username is typically `admin` and the password is `public`. You will be prompted to change the password; set a strong password immediately. Then complete the following in Dashboard:

1. Under **Management → Listeners**, confirm that the TCP listener is running on port `1883`.
2. Under **Access Control → Authentication**, create an appropriate authentication method and MQTT users for this project.
3. Configure the MQTT username and password on the device. Do not allow anonymous clients in production.

Use this broker address on the device:

```text
<EMQX-address>:1883
```

Dashboard login and logout:

1. Open `http://<EMQX-address>:18083/`.
2. On first use, enter username `admin` and password `public`.
3. Follow the prompt to set a new strong password and store it securely.
4. For subsequent logins, use `admin` and the new password.
5. When finished, select the logout option in the user menu at the top right. Do not save passwords on public computers.

If you forget the Dashboard administrator password, reset it from the server terminal. For Docker deployment:

```text
docker exec h743-emqx emqx ctl admins passwd admin '<new-password>'
```

For native Ubuntu deployment:

```bash
sudo emqx ctl admins passwd admin '<new-password>'
```

After the command returns `ok`, log in again using the new password. The password may remain in shell history. In production, handle the relevant history according to your system's security procedures after execution, or use a dedicated temporary administrator account for recovery.

### 3.5 Verify MQTT Publishing and Subscription

Use the cross-platform MQTTX client or the Mosquitto clients on Ubuntu. Install the Ubuntu test clients:

```bash
sudo apt install -y mosquitto-clients
```

Subscribe in terminal 1:

```bash
mosquitto_sub -h localhost -p 1883 -t 'ota/v1/test/#' -v
```

Publish in terminal 2:

```bash
mosquitto_pub -h localhost -p 1883 -t 'ota/v1/test/ping' -m '{"message":"hello"}'
```

If authentication is enabled in EMQX, add `-u '<username>' -P '<password>'` to both commands. Receiving the message in the subscribing terminal confirms that the broker is working.

When testing remote EMQX from your development computer, replace `localhost` with `<EMQX-address>`. Dashboard administrator accounts and MQTT client accounts are managed separately; do not assume they are interchangeable.

## 4. Firewall and Remote Access

Open only the ports needed on each server. For separate hosts, open `8000` on the Caddy host and, as needed, `1883` and `18083` on the EMQX host. On Windows, run the rules applicable to that host in administrator PowerShell:

```powershell
New-NetFirewallRule -DisplayName "H743 Caddy HTTP 8000" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8000 -Profile Private
New-NetFirewallRule -DisplayName "H743 EMQX MQTT 1883" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 1883 -Profile Private
New-NetFirewallRule -DisplayName "H743 EMQX Dashboard 18083" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 18083 -Profile Private
```

On Ubuntu with UFW:

```bash
sudo ufw allow 8000/tcp
sudo ufw allow 1883/tcp
sudo ufw allow 18083/tcp
sudo ufw status
```

Restrict `18083` to the management network instead of exposing it directly to the Internet. For public or production deployments, also enable MQTT TLS, client authentication, and topic authorization, and configure HTTPS for Caddy or place it behind a trusted reverse proxy.

For remote servers, also check cloud security groups, VPN routing, or router port forwarding. The Windows example rules apply to the Private network profile; adjust them for the actual network profile in other deployments. The board must reach both the Caddy download address and the EMQX broker address, but the services do not need to share a LAN.

## 5. Remote Deployment and Project OTA Integration

### 5.1 Publish Firmware to an Independent Server

1. Build, package, and verify the `.fwpkg` on your development computer.
2. Upload the package to the Caddy server using remote-desktop file transfer, SFTP, or your existing release tools. The destination is `D:\Services\Caddy\www\firmware\h743\` on Windows or `/srv/caddy/www/firmware/h743/` on Ubuntu.
3. If the upload account cannot write to the website directory, upload to that account's temporary directory first. Have the server administrator copy it to the destination and grant Caddy read access.
4. Publish version metadata only after the upload finishes so devices cannot download an incomplete package. Use a new versioned filename and verify the size and hash through the actual download URL.

Caddy provides static downloads; this configuration does not provide browser uploads. The server does not need the project's cross-compilation toolchain. EMQX forwards MQTT messages; automatically answering device version queries requires a separate OTA management service. Installing the broker alone does not generate version responses.

### 5.2 Configure the Service Addresses Separately

Point the device's MQTT configuration to `<EMQX-address>:1883` and the firmware URL in the version response to `<Caddy-address>:8000`. When Caddy and EMQX run on separate hosts, use the Caddy address for the firmware URL, not the broker address.

The version response sent to the device can include:

```json
{
  "download_url": "http://<Caddy-address>:8000/firmware/h743/V1.1.0.fwpkg"
}
```

See [`applications/ota/MQTT_BATCH_OTA.md`](applications/ota/MQTT_BATCH_OTA.md) (Chinese) for the complete MQTT topic and JSON message specification. After deployment, check at least the following:

- The board can reach `<Caddy-address>:8000` and download the complete firmware.
- The board can connect to `<EMQX-address>:1883`.
- The firmware URL does not use `localhost`.
- The `.fwpkg` size, CRC32, and SHA-256 match the version response.
- For multiple devices, `rollout_delay_max_seconds` is configured to spread out downloads.

## 6. Troubleshooting

### Port 8000 Is Unreachable

- Check the Caddyfile using `caddy validate`.
- Confirm Caddy loads the configuration file on the server and that the absolute `root` path points to the actual website directory.
- Check whether another application occupies `8000` and whether the system firewall allows access.
- For Ubuntu systemd operation, confirm that the `caddy` user can read `/srv/caddy/www`.

### The Browser Does Not Show a Directory Listing

`file_server browse` displays a listing only when the directory has no `index.html` or `index.txt`. Remove or rename the index file, or visit the firmware subdirectory directly.

### EMQX Dashboard Opens, but the Board Cannot Connect

- Dashboard uses `18083`, while MQTT uses `1883`.
- Check the TCP listener's bind address and running state.
- Check Docker port mappings, firewall rules, credentials, and topic authorization.
- Confirm routing between the board and the EMQX server and use an EMQX IP address or domain name reachable by the device.

### Configuration Disappears After Restarting the EMQX Container

Confirm Compose retains the named volume for `/opt/emqx/data` and that you did not stop it with `docker compose down -v`. Also avoid changing `EMQX_NODE_NAME` without planning: the EMQX data directory is associated with the node name.

## 7. Official References

- [Caddy installation](https://caddyserver.com/docs/install)
- [Caddy `file_server` and `browse`](https://caddyserver.com/docs/caddyfile/directives/file_server)
- [Caddy command line and graceful reload](https://caddyserver.com/docs/command-line)
- [Caddy `basic_auth`](https://caddyserver.com/docs/caddyfile/directives/basic_auth)
- [EMQX Docker installation](https://docs.emqx.com/en/emqx/latest/deploy/install-docker.html)
- [EMQX Ubuntu installation](https://docs.emqx.com/en/emqx/latest/deploy/install-ubuntu.html)
- [EMQX listeners and default ports](https://docs.emqx.com/en/emqx/latest/configuration/listener.html)
- [EMQX administration commands](https://docs.emqx.com/en/emqx/latest/admin/cli.html)
