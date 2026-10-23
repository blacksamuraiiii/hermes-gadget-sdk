# 计划书：Moji 板配置修复、文档一致性修复与 USB 直连（方案 A）

- 适用仓库：`<repo>`（本机 Windows fork，即 hermes-gadget-sdk 的 fork）
- 目标板：`movecall-moji-esp32s3`（ESP32-S3-WROOM-1-N16R8 / GC9A01 240×240 / ES8311）
- 事实快照：2026-10-23
- 证据标记：`[码]` 源码 · `[测]` 本机实测 · `[厂]` 厂商/IDF 文档 · `[推]` 推断 · `[?]` 缺失

---

## 0 摘要

| 编号   | 任务                                   | 性质          | 前置         | 产出                |
| ---- | ------------------------------------ | ----------- | ---------- | ----------------- |
| 阶段 0 | 刷回 hermes 固件并验通 Wi-Fi 全链路            | 验证          | 无          | 一张「已验证」清单         |
| A    | `CONFIG_SPIRAM_IGNORE_NOTFOUND` 隐患修复 | 固件可观测性 + 文档 | 阶段 0       | 2 处代码 + 1 段文档     |
| B    | 文档一致性修复（操作 1–5）                      | 文档/构建注册     | 无（可与 A 并行） | 7 个文件改动           |
| C    | 方案 A：USB 串口隧道                        | 新功能         | 阶段 0 通过    | 4 个固件文件 + 1 个主机脚本 |

**关键决策**：放弃方案 B（USB-NCM），采用方案 A（USB 串口隧道）。理由见第 5 节 —— 决定性证据是这台 Windows 上 `UsbNcm.sys` 根本不存在（设备错误码 28），而 USB-NCM 还要以牺牲 USB-Serial/JTAG 控制台为代价。

**执行顺序**：阶段 0 → 任务 B（低成本、可立即合并）→ 任务 A → 任务 C。任务 C 只有在阶段 0 证明「Wi-Fi 链路本身没问题、只是不想依赖网络」时才启动。

---

## 1 现状事实基线

### 已验证 `[测]`

- 板级配置编译通过；固件烧入后屏幕与 ES8311 双向音频初始化成功；设备自报 `movecall-moji-esp32s3`。
- Flash 16 MB（esptool `flash-id`）、PSRAM 8 MB Octal AP、芯片 rev v0.2。
- 主机侧：Windows 10 企业版 LTSC 19044；WSL2 网关仅在 `127.0.0.1:8765`（`wslrelay.exe`）可达，`192.168.1.4:8765` 不通；主机在以太网 192.168.1.4。
- NCM：`USB\VID_303A&PID_4000&MI_00` 状态 Error，`DEVPKEY_Device_ProblemCode = 28`，`C:\Windows\System32\drivers\UsbNcm.sys` 不存在。

### 未验证

屏幕实际画面、按键说话、Wi-Fi 配网、Hermes 插件与配对、语音全链路。

### 已否定

USB-NCM 在这台 Windows 上免驱可用。

---

## 2 阶段 0：前置验证（必须先做）

理由：任务 A/B 的验收依赖真实启动日志，任务 C 的价值判断依赖「Wi-Fi 链路是否本来就能用」。当前板子跑的是 `tusb_ncm` 示例，COM5 不存在。

### 2.1 刷回 hermes 固件

```text
1. 按住 BOOT 不放
2. 插 USB 线（或断电再上电）
3. 松开 BOOT   → ROM 下载模式，USB-Serial/JTAG 重新枚举为 COM5
4. 打开 http://localhost:8765/，选 COM5，烧录
```

⚠️ **合并镜像写 0x0 会清空 NVS**：设备名、配对关系、设备身份、已存 Wi-Fi 全部丢失，需要重新配网与 `hermes gadget pair`。若想保留，改用 `-app.bin` 写 OTA 槽位的方式（不推荐，跨板型切换时应整片擦除）。

页面打不开时重启静态服务：

```bash
cd ~/flash-site && /usr/bin/python3 -m http.server 8765 --bind 0.0.0.0
```

### 2.2 免配网并验通

```bash
# 编译期写死 Wi-Fi：上电即连，手机配网流程不再出现
idf.py menuconfig   # Hermes Gadget → Defaults → Wi-Fi SSID / password
```

```powershell
# 管理员 PowerShell：把 WSL2 网关暴露到局域网（WSL IP 重启后会变，需重做）
netsh interface portproxy add v4tov4 listenaddress=192.168.1.4 listenport=8765 `
  connectaddress=(wsl hostname -I).Trim() connectport=8765
New-NetFirewallRule -DisplayName "Hermes Gadget 8765" -Direction Inbound `
  -Protocol TCP -LocalPort 8765 -Action Allow
```

### 2.3 阶段 0 完成判据

1. 串口日志出现 `parts: display gc9a01, microphone es8311, speaker es8311`。
2. `diag` 显示 `phase=online`。
3. 按住 BOOT 说一句话，Hermes 回话从喇叭出来。
4. 换一个不同 SSID（同路由器访客网络）仍能连上。

> 第 4 条是关键：它证明「同一 Wi-Fi」这条限制来自网络暴露，而不是固件。若第 4 条失败，先查 portproxy 与防火墙，不要动 USB。

---

## 3 任务 A：`CONFIG_SPIRAM_IGNORE_NOTFOUND` 隐患修复

### A.1 问题重述（含对上一轮结论的订正）

`firmware/esp32/boards/movecall-moji-esp32s3/sdkconfig.defaults:15` 设置了 `CONFIG_SPIRAM_IGNORE_NOTFOUND=y`，含义是「PSRAM 初始化失败也继续启动」`[码]`。

上一轮我把后果写成「黑屏且日志干净」，**这个描述不准确，需要订正**：

```c
// port_display.cpp:87-92
const size_t px = cfg.width * cfg.height;
fb_ = heap_caps_malloc(px * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (!fb_) fb_ = heap_caps_malloc(px * 2, MALLOC_CAP_8BIT);   // ← 内部 RAM 回退，无日志
bounce_ = heap_caps_malloc(cfg.width * kBounceRows * 2, MALLOC_CAP_DMA);
if (!fb_ || !bounce_) { ESP_LOGE(TAG, "not enough memory for a %ux%u framebuffer"); return false; }
```

真实行为是**静默降级**：240×240×2 = 112.5 KB 会改从内部 RAM 拿，屏幕仍然点亮，日志里没有任何警告。所以症状不是黑屏，而是：

1. 内部 RAM 被占走 112.5 KB，Wi-Fi + 音频缓冲吃紧 → 播放断续、握手失败；
2. 日志干净 → 排障时完全看不到根因，`hardware.md:50` 又把黑屏归因到面板表/背光极性，方向被带偏。

另外：**全仓 19 块板都设了这个选项**，它是仓库级约定，不是 Moji 独有错误。因此修法不能只改 Moji 一行，否则制造新的不一致。

### A.2 选项对比

| 选项 | 做法                                                      | 优点                     | 缺点                                  | 结论           |
| -- | ------------------------------------------------------- | ---------------------- | ----------------------------------- | ------------ |
| A1 | 保留约定，补可观测性（回退时 `ESP_LOGW` + 启动期 PSRAM 为 0 时 `ESP_LOGE`） | 不破坏一致性；故障可见；`diag` 仍能跑 | 仍是降级运行                              | **采纳**       |
| A2 | Moji 去掉 `IGNORE_NOTFOUND`，PSRAM 失败即在启动时 abort           | 故障显性、不可能带病运行           | 与其余 18 块板不一致；现场只能看 abort，拿不到 `diag` | 不采纳（记为板级可选项） |
| A3 | 让帧缓冲强依赖 PSRAM、失败即放弃显示                                   | 逻辑简单                   | 现状已优于它（有回退）；会让 4 MB 无 PSRAM 的板直接不可用 | 不采纳          |

### A.3 采纳方案 A1 的具体改动

**改动 1 — `firmware/esp32/main/port_display.cpp`（约 :89）**

```c
  fb_ = static_cast<uint16_t*>(heap_caps_malloc(px * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!fb_) {
    fb_ = static_cast<uint16_t*>(heap_caps_malloc(px * 2, MALLOC_CAP_8BIT));
    if (fb_) {
      // PSRAM is configured but did not come up (or is full): this board is now
      // running the panel from internal RAM, which competes with Wi-Fi and the
      // audio buffers. Say so, or the only symptom is stuttering sound.
      ESP_LOGW(TAG, "no PSRAM framebuffer; %u KB of internal RAM taken for the panel",
               static_cast<unsigned>(px * 2 / 1024));
    }
  }
```

**改动 2 — `firmware/esp32/main/port_diag.cpp`（`log_boot_summary`，约 :178）**

```c
  const size_t spiram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  if (spiram_total == 0) {
    ESP_LOGE(TAG, "PSRAM configured but none initialised: the panel runs from internal RAM, "
                  "and audio may stutter (CONFIG_SPIRAM_IGNORE_NOTFOUND turns this into a warning)");
  }
```

**改动 3 — `firmware/esp32/boards/movecall-moji-esp32s3/sdkconfig.defaults`（:15 上方注释）**

```diff
-# If PSRAM ever fails to start, boot anyway so the log and `diag` can say so.
+# If PSRAM ever fails to start, boot anyway so the log and `diag` can say so.
+# Consequence: the 240x240 framebuffer (112.5 KB) then comes from internal RAM
+# and competes with Wi-Fi and the audio buffers. port_display logs a warning and
+# port_diag logs an error in that case; docs/hardware.md#movecall-moji-esp32s3
+# says what that looks like. Removing this option makes the board abort at boot
+# instead, which is a deliberate board-level choice, not a bug fix.
 CONFIG_SPIRAM_IGNORE_NOTFOUND=y
```

**改动 4 — `docs/hardware.md:50` 排障条目改为**：

```markdown
- The screen stays black but the log is clean: the panel may need its own `Gc9a01Panel`
  table, or the backlight polarity is inverted on this glass revision. If the log instead
  says `no PSRAM framebuffer` or `PSRAM configured but none initialised`, PSRAM did not
  come up: the panel is running from internal RAM and audio will stutter — that is a
  hardware/PSRAM problem, not a panel problem.
```

### A.4 验收判据

| 场景                                     | 期望                                                                                                                |
| -------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| 正常板                                    | 日志无 `no PSRAM framebuffer`；`memory:` 行显示 `PSRAM 8192 KB of 8192 KB`                                               |
| 人为制造 PSRAM 失败（临时 `CONFIG_SPIRAM=n` 构建） | 出现 `no PSRAM framebuffer; 112 KB of internal RAM taken` + `PSRAM configured but none initialised`；面板仍点亮；`diag` 可用 |
| 两种情况下 `check_config.py`                | 均通过（不新增 defaults 行，只改注释）                                                                                          |

---

## 4 任务 B：文档一致性修复（操作 1–5）

任务背景：板级移植已完成 `board.cpp` / `Kconfig` / `boards/<board>/*` / `hardware.md` 四处，但 `CONTRIBUTING.md#adding-a-board` 清单里的另外 5 项未做。

### B.1 操作 ①：补 PlatformIO 环境（阻断级）

- 问题：`platformio.ini` 现有 18 个 `[env:*]`，无本板；`package_release.py` 只认 `pio env ∪ IDF_BOARDS`，本板不在 `IDF_BOARDS`（仅 `crowpanel-p4-101`）`[码]`。
- 后果：不进 CI → 不打包 → `manifest.json` 无此项 → 浏览器安装器选不到本型号；`setup-board.md:31` 的引导对本板无效。
- 改动：`firmware/esp32/platformio.ini` 追加

```ini
; Movecall Moji: ESP32-S3-WROOM-1-N16R8, 16 MB flash, 8 MB octal PSRAM,
; round 240x240 GC9A01 over SPI, one ES8311 with an analog microphone.
[env:movecall-moji-esp32s3]
board = esp32-s3-devkitc-1
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.cmake_extra_args =
    -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/movecall-moji-esp32s3/sdkconfig.defaults"
```

- 关键：`flash_size` 必须 16MB。PlatformIO 会用它覆盖 sdkconfig，若留默认 4MB，与板级 defaults 的 16MB 冲突，`check_config.py` 直接 `FATAL_ERROR`。
- 验收：`pio run -e movecall-moji-esp32s3` 通过；CI `esp32` job 矩阵出现该 env；`tests/test_firmware_tools.py::test_every_real_board_has_what_the_installer_shows` 由「空转通过」变为真正覆盖本板。

### B.2 操作 ②：给板节补构建/烧录命令

- 问题：`docs/hardware.md:15-52` 的 Moji 节没有构建命令，而 LCD-1.54（:111）、1.85C（:146）、AMOLED-1.75C（:270）都有；通用 `## Build and flash`（:791）只给 breadboard `[码]`。
- 改动：在 `### First flash: what to check`（:47）前插入

```bash
cd firmware/esp32
pio run -e movecall-moji-esp32s3 -t upload -t monitor
# 或用 ESP-IDF（本板 console 走 USB Serial/JTAG，与烧录同一个口）：
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/movecall-moji-esp32s3/sdkconfig.defaults" build
idf.py -p COM5 flash monitor
```

- 验收：命令可直接复制执行；`hardware.md` 内 `#build-and-flash` 与本节的交叉引用成立。

### B.3 操作 ③：修正两处与代码不符的判据（事实错误）

- 问题 1：`hardware.md:49` 写「The log must show `HGBOARD=movecall-moji-esp32s3`」。固件**从不打印**该字符串 —— 它是编译进镜像的常量（`board.cpp:54`），只被 `port_ota.cpp:77` 用来拒装别的板型镜像 `[码]`。
- 问题 2：同句「A wrong board image refuses to run」。实际是**拒绝安装**；烧错镜像到 0x0 仍会启动，只是引脚全错。
- 改动：

```markdown
- The boot log must show `gc9a01 240x240 ready`, then
  `codecs: speaker ready, microphones ready (es8311 analog)`, and `parts: display gc9a01,
  microphone es8311, speaker es8311`. The board name never appears in the log: the image
  carries it as the `HGBOARD=` tag, which only the updater uses to refuse another board's
  firmware. A wrong image still boots — it just drives the wrong pins.
```

- 验收：按新判据对照真实启动日志，逐条命中；`grep -r "HGBOARD" docs/` 只剩这一处且表述正确。

### B.4 操作 ④：补两张表与变更日志

- 问题：`docs/supported-hardware.md`、`docs/hardware-validation.md`、`CHANGELOG.md` 对 `moji` 全文 0 命中 `[码]`。
- 改动：

```markdown

| [Movecall Moji](hardware.md#movecall-moji-esp32s3) | Hold BOOT to talk; no cancel key | Onboard analog microphone and speaker; experimental |
```

```markdown

| Movecall Moji (ESP32-S3-WROOM-1-N16R8, rev v0.2) | GC9A01 240×240 round; BOOT only | One ES8311: AP2718AT analog microphone, NS4150B speaker | USB power only; no battery, charger or gauge | Experimental; no physical report, and no CI build until its PlatformIO environment exists |
```

```markdown

- Board support for the Movecall Moji: round 240×240 GC9A01 over SPI, one ES8311 for the
  speaker and an analog microphone on its own ADC, BOOT key only. Adds a
  `movecall-moji-esp32s3` profile. Experimental: builds and initialises display and audio,
  but the screen, hold-to-talk, Wi-Fi setup, pairing and a full voice turn are not verified.
```

- 验收：CI 的 changelog 检查通过；`site/scripts/docs.mjs` 导航无需改动（未新增页面）。

### B.5 操作 ⑤：补第三方来源署名

- 问题：`NOTICE` 只署名了 Spotpear 两块板，未署名本板来源；`THIRD_PARTY_NOTICES.md:18` 的 GC9A01 行只写「Spotpear 1.28-BOX round panel」，而本板同样使用该驱动 `[码]`。
- 改动：`NOTICE` 在 Spotpear 段后追加

```text
The Movecall Moji pin map and panel settings in firmware/esp32/main/board.cpp follow
xiaozhi-esp32's movecall/moji-esp32s3 board,
https://github.com/78/xiaozhi-esp32/tree/d91d6824309d12df5fc3db5edf8145ee829135d7/main/boards/movecall/moji-esp32s3,
MIT License. Only these hardware facts are used.
```


`THIRD_PARTY_NOTICES.md` 的 GC9A01 行改为「Spotpear 1.28-BOX and Movecall Moji round panels」。

- 验收：`tools/package_release.py` 打出的 licenses 包内含更新后的 `NOTICE`；`grep -i moji NOTICE` 命中。

### B.6 顺带项（非阻断，建议同 PR）

`docs/hardware.md:7-12` 的 Requirements 写死了「SPI ST7789 panel」「I2S MEMS microphone」，与本板（GC9A01 + 模拟麦）相反，建议改为「默认参考构型」并指向下方板节，避免读者误判。

---

## 5 决策：为什么放弃方案 B，选择方案 A

### 5.1 方案 B（USB-NCM）的否定证据

| 检查项 | 结果 | 含义 |
|---|---|---|
| 设备状态 | `USB\VID_303A&PID_4000&MI_00`，Status Error `[测]` | 枚举成功但未绑定驱动 |
| `DEVPKEY_Device_ProblemCode` | **28**（未安装驱动）`[测]` | 不是配置错误，是缺驱动 |
| `C:\Windows\System32\drivers\UsbNcm.sys` | **不存在** `[测]` | 连手写 INF 指向 inbox 服务都无门 |
| `C:\Windows\INF\usbncm.inf` | 存在但无对应二进制 `[测]` | 印证上一条 |
| `rndismpx.sys` | 也缺（`usb8023.sys` 在）`[测]` | 改 RNDIS 大概率同样缺件 |
| 系统版本 | Windows 10 LTSC 19044 `[测]` | 无 `networkingMode=mirrored`，NAT 下必须额外转发 |

结论：B 的可行性不掌握在自己手里（需第三方签名内核驱动或更换系统），且 `[?]` 项太多。

### 5.2 结构性代价对比

| 维度 | 方案 B：USB-NCM | 方案 A：串口隧道 |
|---|---|---|
| 驱动依赖 | 需要 NCM/RNDIS 驱动（本机缺） | 无（CDC-ACM 免驱） |
| USB 外设占用 | USB-OTG，**与 USB-Serial/JTAG 二选一** | 沿用现有 USB-Serial/JTAG |
| 控制台/`diag` | 失去（当前每次刷机需按 BOOT） | 保留 |
| esptool / 安装器 | 只能走 ROM OTG 下载，`[?]` 未验证 | 完全不变 |
| 主机侧改动 | 虚拟网卡静态 IP + portproxy + 防火墙 | 一个 Python relay，直连 127.0.0.1 |
| 固件改动 | tinyusb net + netif + 链路状态伪装 | 一个 Transport 实现 + console 单读者分流 |
| 协议层改动 | 无 | 无（帧只承载同一套 JSON/二进制） |
| 故障可观测性 | 差（无 console） | 好（console 与 relay 日志并存） |

### 5.3 决策与重新评估条件

**采纳方案 A。** 重新评估 B 的条件只有三条同时成立：
1. 目标机器上确认存在可用且有签名的 NCM/RNDIS 驱动；
2. 该板已不再需要 USB 串口控制台（例如 console 迁到 UART0 排针）；
3. 确实需要「一个 IP 端点」而非「一条数据通道」（例如要做多设备同网段发现）。

在此之前，B 的任何投入都是沉没成本。

---

## 6 任务 C：方案 A 详细实施

### C.0 架构

```
Moji(ESP32-S3) ──USB CDC(303A:1001, SLIP 帧)── Windows relay ──ws://127.0.0.1:8765/gadget── WSL2 Hermes
                        ↑ 同一个口仍可收发 console 行与烧录
```

- 设备不需要 IP，Wi-Fi 可关。
- relay 在 **Windows**，因为本机 8765 只有 `127.0.0.1` 可达（`wslrelay`），WSL2 网络零改动。
- relay 只做 WS 客户端 + 串口透传，不解析业务 JSON；token 在 `hello` 报文体内（`app.cpp:421`），子协议固定 `hermes-gadget.v1`（`protocol.py:19`）。

### C.1 帧格式（SLIP）

```
帧 = 0xC0 | type(1B) | payload | 0xC0
type: 0x01 文本(JSON)  0x02 二进制(音频)  0x03 关闭  0x04 心跳
payload 内 0xC0→0xDB 0xDC，0xDB→0xDB 0xDD
```

- 选择 SLIP 而非自定义长度头：与 console 行共存时只靠首字节 0xC0 即可区分，且实现不足 60 行。
- 最大帧：JSON 控制帧 < 4 KB；二进制音频 40 ms @24 kHz PCM16 = 1920 B，留 8 KB 上限。

### C.2 固件改动

| 文件 | 改动 | 要点 |
|---|---|---|
| `main/Kconfig.projbuild` | 新增 `HG_LINK_USB_SERIAL`（bool，默认 n）、`HG_LINK_USB_PATH`（string，默认 `"/gadget"`） | 与 Wi-Fi 路径互斥；`depends on` 不加，允许调试时同时编进去 |
| `main/port_link.cpp`（新增） | `class SerialLink final : public hg::Transport` | 实现 `connect/send_text/send_binary/close`；rx/tx 两条任务；复用 `hg::ws::Generations` 与 `TxWatch`（`drivers/ws_link.hpp`）保持与 WS 版一致的代数/超时语义 |
| `main/port_link.cpp` | `begin()` 安装 `usb_serial_jtag_driver_install()`，启动单读者任务 | 帧 → `EventType::WsText/WsBinary`；收到 HELLO_ACK → `WsOpen`；链路断 → `WsClosed` |
| `main/port_console.cpp` | `begin()` 在 `HG_LINK_USB_SERIAL` 下**不**启动 REPL；由 link 任务把非帧字节累积成行 | 行进 `ConsoleRequest` + `events::post(EventType::Console, …)` 路径（与 `:30-38` 完全一致），回复直接写回串口；`help/diag/wifi/server` 全部保留 |
| `main/main.cpp` | `:271` 处按 Kconfig 选 `hal.transport`；`:267` 的 `g_wifi.begin()` 在该模式下跳过（或保留但不进配网 AP） | 收到 ACK 后 `events::post(EventType::NetUp, "USB", 3)`，让 `app.on_network(true,"USB")` 离开 `Phase::NoNetwork`（`app.cpp:259/324`） |
| `main/main.cpp` | 关闭 Wi-Fi 相关 UI 入口时保持 `on_wifi_setup` 为空 | 避免设备弹配网页 |

**语义约定**
- `connect(url, subprotocol)` 忽略 URL 的 host，只取 path（若为空用 `HG_LINK_USB_PATH`），放进 HELLO：`{"v":1,"sub":"hermes-gadget.v1","path":"/gadget"}`。这样设备上原有的 `server` 设置**不需要改**，同一配置在 Wi-Fi 与 USB 下都成立。
- 重连由 `hg::App` 的退避驱动，`SerialLink` 只负责把 `close()` 变成发 `0x03` 并清队列。
- 心跳：`hg::ws::kNetworkTimeoutMs = 10 s`、`kStuckMs = 60 s`（`ws_link.hpp:54/65`）沿用；relay 侧 30 s 无流量则主动断，触发设备重连。

### C.3 主机 relay（`tools/usb_relay.py`，新增）

```python
# 伪代码，约 150 行
# 1) 按 VID_303A & PID_1001 找口（不要写死 COMx，重新枚举后会漂）
# 2) ser = serial.Serial(port, 115200, timeout=0)
#    ser.dtr = False; ser.rts = False      # 必须先做：DTR/RTS 会把板子复位
# 3) ws = websockets.connect("ws://127.0.0.1:8765" + path,
#                            subprotocols=["hermes-gadget.v1"], max_size=None)
# 4) 双向泵：串口帧 → ws.send(text|bytes)；ws.recv() → 串口帧
# 5) 任一侧断开：关闭另一侧，2 s 后退避重连
```

运行方式（Windows，建议做成计划任务/开机启动）：

```powershell
python tools\usb_relay.py --target ws://127.0.0.1:8765 --path /gadget
```

### C.4 使用流程

```bash
# 1) 编译（本机 Windows fork 内）
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/movecall-moji-esp32s3/sdkconfig.defaults" `
       -DHG_LINK_USB_SERIAL=y build     # 或 menuconfig 勾选
idf.py -p COM5 flash

# 2) 设备侧不需要配 Wi-Fi；server 保持原值即可
# 3) Windows 起 relay，等日志出现 hello ack
# 4) 按住 BOOT 说话
```

### C.5 验证清单

| # | 检查 | 判据 |
|---|---|---|
| 1 | 关 Wi-Fi（`wifi off` 或路由器关机）后仍可用 | 完成一次完整问答 |
| 2 | console 未被破坏 | 同一次连接里敲 `diag`，能拿到 `parts:` 与 `memory:` |
| 3 | 链路吞吐 | 双向 48 KB/s 连续 10 min，无 `stuck`（60 s 阈值） |
| 4 | 拔插恢复 | 拔 USB → 重插，relay 自动重连 ≤ 5 s，设备回到 online |
| 5 | 烧录不受影响 | relay 关闭后 `idf.py flash monitor` 正常 |
| 6 | 配对 | `hermes gadget pair` 走隧道成功 |
| 7 | OTA | `hermes gadget update --latest` 走隧道成功（board tag 拒装逻辑仍生效） |
| 8 | 边界 | 同时搜 COM 口的 esptool 与 relay 会冲突 → relay 需在文档里声明独占 |

### C.6 回滚

- 固件：`HG_LINK_USB_SERIAL` 默认 n，恢复即回到 Wi-Fi 路径，无需删代码。
- 主机：关掉 relay 进程即可。
- 不涉及分区表、NVS 布局、OTA 通道，`type=movecall-moji-esp32s3` 不变。

---

## 7 风险登记

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| R1 | relay 与 esptool/安装器争用 COM 口 | 高 | 文档声明独占；relay 检测到 esptool 打开失败即退出并提示 |
| R2 | DTR/RTS 导致开串口即复位 | 中 | relay 打开后立即置 False；验证清单 #4 覆盖 |
| R3 | 单读者分流实现不当，console 与帧互相污染 | 中 | 只用首字节判帧；行长上限 256；越界丢行并提示 |
| R4 | USB 线质量/供电导致掉线 | 中 | 重连退避 + 日志计数；换线验证一次 |
| R5 | `HG_LINK_USB_SERIAL` 与 Wi-Fi 同时开启时状态机打架 | 中 | 该模式下不启动配网 AP，`on_network` 只由 link 触发 |
| R6 | 后续 IDF 版本改了 `usb_serial_jtag` API | 低 | 与 `port_console.cpp` 同一组 API，升级时一起改 |
| R7 | 任务 A 的日志改动被误读为「PSRAM 坏了就是软件 bug」 | 低 | 日志文案明确写「hardware/PSRAM problem」 |

---

## 8 待验证假设

1. `[?]` 本板 USB-Serial/JTAG 在 relay 持续占用下，长报文（8 KB 帧）不丢字节 —— 需实测。
2. `[?]` 内部 RAM 回退在真实 PSRAM 故障时能成功（112.5 KB 连续块）—— 只能用 `CONFIG_SPIRAM=n` 近似验证。
3. `[?]` `hg::ws::Generations` / `TxWatch` 能被 SerialLink 直接复用而不引入 WS 依赖。
4. `[推]` relay 侧 `websockets` 库对 `max_size=None` 与二进制帧的处理与固件 `kBufferSize=4096` 兼容。
5. `[?]` 设备在 USB 链路下 `Phase::NoNetwork` 的文案是否需要新增 i18n 条目（「USB」状态）。

---

## 9 交付物与执行顺序

1. **阶段 0**：刷回固件 + Kconfig Wi-Fi + portproxy → 产出「已验证」清单（未完成的 5 项逐条销账）。
2. **任务 B**（1 个 PR）：`platformio.ini`、`docs/hardware.md`（②③⑤ 段落）、`docs/supported-hardware.md`、`docs/hardware-validation.md`、`CHANGELOG.md`、`NOTICE`、`THIRD_PARTY_NOTICES.md`。
3. **任务 A**（1 个 PR）：`port_display.cpp`、`port_diag.cpp`、板级注释、`docs/hardware.md:50`。
4. **任务 C**（1 个 PR，仅在阶段 0 通过后）：`Kconfig.projbuild`、`port_link.cpp`、`port_console.cpp`、`main.cpp`、`tools/usb_relay.py`，以及 `docs/hardware.md` 新增「USB link」小节。

每个 PR 都遵循 `CONTRIBUTING.md`：同一 PR 只做一件事、同步文档、跑 `pytest tests/test_firmware_tools.py` 与受影响板的 `pio run`、在 PR 描述里区分「编译通过 / 模拟器 / 实机」三类证据。
