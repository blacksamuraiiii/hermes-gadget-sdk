# Hermes Gadget 中文说明（moji 板）

本仓库是 [Adolanium/hermes-gadget-sdk](https://github.com/Adolanium/hermes-gadget-sdk) 的 fork，
把一块 ESP32-S3 圆屏小板（`movecall-moji-esp32s3`，墨迹板）做成 **Hermes Agent 的桌面提醒屏 + 语音终端**：
任务需要人介入或跑完时，屏幕出卡片、喇叭出提示音；喊「你好小墨」就能直接对话。USB 线直连，不用配网。

上游英文文档见 [README.md](README.md) 与 [docs/](docs/)；逐条改动见 [CHANGELOG.md](CHANGELOG.md)；
设计、决策与实测记录在 [notes/](notes/README.md)。

---

## 一、版本更新记录

### moji-0.3 · 预发布（`feat/moji-0.3`，固件 `0.3.0`）

GitHub 上是 **pre-release**：`hermes gadget update --latest` 只认正式发布，取不到这一版；
唤醒阈值还没标定，所以没转正式版。资产与刷法见 [firmware/releases/v0.3/](firmware/releases/v0.3/README.md)。

**固件**

- 唤醒引擎跟着麦克风走：播报结束后不必等回合落定就能再唤醒（0.2 里有约 30 秒死窗）。
- 唤醒阈值恢复 `CONFIG_HG_WAKE_THRESHOLD / 100`（0.20），启动日志打印**实际值**，不再打配置值。
- bring-up 刷屏日志收进 `CONFIG_HG_WAKE_DIAG`（默认关）：默认构建的日志只剩 `hit:` 与生命周期行。
- tap 持麦期间不再投 `Mic` 事件：空闲时每秒上百次分配/拷贝没了，也不再和真实音频抢 96 深队列。
- 网关重启期间不再弹 `connect timeout`：连续 3 次未握手才报错，前两次只在后台重试。
- 唤醒引擎启动失败时释放已建的模型/AFE/队列，不再把 PSRAM 和模型分区的 SPIFFS 句柄留到下次重启。

**插件**

- 温度读作「摄氏度」：Hermes 的归一化把 `22°C` 展开成英文 `degrees Celsius`，中文音色读成 deg C；
  现在口播用「22摄氏度」，中文屏仍显示 `22°C`（ASCII 屏照旧折成 `deg`）。
- 提醒按 `request_id` 去重，不再按 cue 全局去重：两条不同 approval 相隔不到 4 秒时，第二条不再被吞。
- `hermes gadget show <设备> "标题" "正文" --ttl 20`：进程外推卡片，不起回合、不烧 token。
- **天气**：`hermes gadget weather [设备]` 把实时天气 + 3 天预报推到屏幕，agent 侧对应工具
  `gadget_weather`（语音问「今天天气」直接答，不再走搜索）。数据源和风天气（中文原生、带官方预警），
  配置只读环境变量：`QWEATHER_HOST` / `QWEATHER_KEY` / `QWEATHER_PLACE`，写进 profile 的 `.env`。
- 无 dashboard 的板（本板）在 Hermes 等待批准时也会出「等待你确认」卡片。
- 每 2 分钟的 relay 巡检**默认关闭**（`platforms.gadget.extra.relay_watch: true` 才开）：板子不是一直插着。
- 口播与屏幕都先清洗模型当文本吐出的工具调用标记，整条回复都是标记时给「这条回复是空的，请再问一次」。

**还没做**：阈值标定（发版阻断）、E2/E3/E7 真机记录、B5 的失败注入实测。

### moji-0.2 · 已发布（tag `moji-0.2`）

- **语音唤醒打通**：esp-sr MultiNet5（`mn5q8_cn`，模型在 4 MB 的 `model` 分区），两条命令词
  `ni hao xiao mo`（你好小墨）/ `xiao mo xiao mo`（小墨小墨）；codec 24 kHz → `rate_cvt` 16 kHz → 单麦 AFE → MultiNet。
- **「我在」提示音**：命中后先发 `wake.hit`、亮屏，宿主播 `wake.wav`（1.22 s），**播完**才开录音——
  单麦无 AEC，边播边录会把提示音自己录进去。
- **分区表变更**：app 涨到 2.1 MB 撑破旧槽，两个 ota 槽放大到 `0x3e0000`、`model` 上移到 `0x800000`；
  **从 0.1 升级必须整片刷一次**，之后 OTA 才继续可用。
- **中文点阵字库**：Fusion Pixel 12px（GB2312 一级 3755 字 + 6×12 ASCII），汉字占 2 列，240 圆屏上是 26 列 × 5 行。
- **提醒桥**：`approval` / `clarify` / `done` 三类钩子 → cue + 卡片，不起回合、不调 STT/TTS。
- relay 生命周期挂到 gateway：`connect()` 拉起、`disconnect()` 停下，Windows 侧 `pythonw.exe` 无窗口。

### moji-0.1 · 已发布（tag `moji-0.1`）

- USB 线直连：板子原生 USB Serial/JTAG → `tools/usb_relay.py` → gateway 内 hub（`127.0.0.1:8765/gadget`），
  跳过 Wi-Fi 配网。回 Wi-Fi 只需注释掉 `CONFIG_HG_LINK_USB_SERIAL` 重编，代码不用改。
- 三类提醒 + 消息网关 + 屏幕卡片（英文 5×7 字形，26 列 × 10 行）。

### 与上游的关系（为什么 GitHub 显示「落后 284」）

分叉策略是逐条 `git cherry-pick -x`，**不整体 merge 上游 `main`**（字体层、adapter、板级三处大分叉）。
所以「284 commits behind」是分叉深度，不是漏同步：已评估到 upstream `24d21a9`（2026-10-29）。
与共享层相关、可能值得移植的只有 `f481640` + `2075171` + `ea205f4`（播报缓冲 + 插件提前量，USB 直链收益有限）；
其余是彩色脸/帧图、camera PNG、Tailscale、PSRAM TCP 窗口，Skip 理由都在 `CHANGELOG.md`。

---

## 二、刷机步骤

### 0. 前提

- Windows 侧，板子走 USB-C 数据线；WSL 里跑 gateway。
- 固件与 `srmodels.bin` 在 [firmware/releases/](firmware/releases/)；0.3 用 `v0.3/`。
- relay 占着 COM5，刷前先停：`powershell -NoProfile -ExecutionPolicy Bypass -File tools\relay_stop.ps1`。

### 1. 刷固件

**只刷 app**（0.2 → 0.3 推荐，分区表没变）：

```powershell
python -m esptool --chip esp32s3 -p COM5 -b 460800 --before default-reset --after hard-reset `
  write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB `
  0x20000 hermes_gadget.bin
```

**整片刷**（0.1 → 0.2/0.3、换板子、或要重写模型分区）：

```powershell
python -m esptool --chip esp32s3 -p COM5 -b 460800 --before default-reset --after hard-reset `
  write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB `
  0x0      bootloader.bin `
  0x8000   partition-table.bin `
  0xf000   ota_data_initial.bin `
  0x800000 srmodels.bin `
  0x20000  hermes_gadget.bin
```

刷完 `powershell -NoProfile -ExecutionPolicy Bypass -File tools\relay_start.ps1`。
`--flash-size 16MB` 必带：esptool 5.6.0 不带它会把 `0x800000` 判成越界。

### 2. 装插件

把 `plugin/` 放到 `~/.hermes/plugins/gadget`（复制或符号链接）。设备跑在 `gadget` profile 时，
该 profile 的 `plugins/` 目录下要有这几个插件（符号链接即可），否则 `web` 等工具集解析为空：

```bash
ln -sfn ~/.hermes/plugins/anysearch            ~/.hermes/profiles/gadget/plugins/anysearch
ln -sfn ~/.hermes/plugins/gadget               ~/.hermes/profiles/gadget/plugins/gadget
ln -sfn ~/.hermes/plugins/hermes-stt-sensevoice ~/.hermes/profiles/gadget/plugins/hermes-stt-sensevoice
```

### 3. 写配置（`platforms.gadget`）

```yaml
platforms:
  gadget:
    enabled: true
    home_channel: hg-xxxxxxxxxxxx          # 设备 id（hermes gadget devices）
    extra:
      voice: true                          # 唤醒后要能对话；false 只当提醒屏
      reminders: true
      reminder_min_turn_s: 45
      relay_start: tools/relay_start.ps1
      relay_stop: tools/relay_stop.ps1
      # relay_watch: true                  # 需要每 2 分钟兜 relay 崩溃时才开，默认关
```

提示音文件用 `tools/make_cues.py` 生成（`approval` / `clarify` / `done` / `wake`），落在
`~/.hermes/plugin-data/gadget/cues/`。天气功能另需三个环境变量，写在 profile 的 `.env` 里
（不进 git）：`QWEATHER_HOST`（控制台「设置」里的 API Host）、`QWEATHER_KEY`（项目管理里的 API KEY）、
`QWEATHER_PLACE`（默认扬州；也可用 `QWEATHER_LAT` / `QWEATHER_LON` 直接指定坐标）。
`tools/relay_start.ps1` 的 Python 解释器取 `RELAY_PYTHONW`，
否则找用户目录下名为 `gadget-relay` 的 conda 环境（需有 `pyserial` 与 `websockets`）；`tools/relay.sh` 认 `RELAY_ENV_DIR`。

### 4. 起 relay 与验收判据

WSL 侧 `tools/relay.sh start`（无窗口），或让 gateway 自己拉。判据都靠设备自报，不靠肉眼：

| 项 | 判据 |
|---|---|
| 链路 | `tools/relay.log`：`serial COM5 open` → `hg: online (paired)` |
| 唤醒命中 | `hg.wake: hit: command 0 at NN percent`，屏幕进 Listening |
| 「我在」播出 | `hg.codec: playback ended ... 58752 B played`，且**播完才**出现录音帧 |
| 唤醒后能对话 | `hg: recording: end, N chunks` → 宿主回复 → 播报结束 |
| 提醒出声 | `hg.codec: playback ended ... N B played`，N 与对应 cue 的 wav 字节数一致 |
| 卡片 | 下行 `display {"title":...}`；中文固件上标题为中文 |
| 熄屏后提醒 | 设备自报 `state display_sleeping:true`，播报时回 `false` 并出卡片 |
| 引擎在持麦 | 开 `CONFIG_HG_WAKE_DIAG` 后每 200 次 tap 一条 `hg.wake: tap: ... peak N`，`peak` 随人声起伏；默认构建没有这行 |
| 空转写/静音不启回合 | 按住不到 0.5 s 或录到房间噪声时设备回「没听清，请按住按键说话」，`gateway.log` 无对应 inbound |
| console 命令 | 一律走 `tools/console_cmd.py`（自动停 relay、等启动就绪），回复以 `@` 开头，期间无 `PermissionError(13)` |
| 字号 | `hermes gadget devices` 上报 `text_cols=26, text_rows=5` |

---

## 三、已知问题

每条按「现象 / 处理 / 验收」写。

| 现象 | 处理 | 验收 |
|---|---|---|
| 罕字显示 `?`（GB2312 一级以外） | 字库已扩到 GB2312 全表：实收 6647 字（一级 3755 全覆盖、二级 2892/3008），剩余 116 个二级字是源 BDF 本身没有 | 卡片里实际出现的罕字不再 `?`；`tools/make_cjk_font.py` 会打印缺字清单 |
| 打开 COM5 会复位设备，启动头 2–4 秒内发 console 命令回 `@error ESP_ERR_INVALID_STATE` | `tools/console_cmd.py` 默认 `--wait-ready`：等启动行再发，命中该错误自动重试 3 次 | 输出 `@ok` / `@value`，不再出现 `ESP_ERR_INVALID_STATE` |
| console 命令与 relay 抢同一个串口 | `console_cmd.py` 先停 relay、发完再起，无需手工 `taskkill` | 期间无 `PermissionError(13)`；发完 `relay.log` 重新 `online (paired)` |
| relay 不支持从 stdin 敲命令（裸字节插进 SLIP 帧中间会打坏帧） | 一律走 `tools/console_cmd.py`，relay 只负责帧转发 | 无坏帧/JSON 解析失败；console 回复都是 `@` 开头单行 |
| `tools/usb_relay.py --reset` 让板子静默（RTS 复位后 usb-serial-jtag 不再应答） | 不要用 `--reset`；要重启就断电或复位整板 | 不会出现反复 `serial reopen failed (port missing)` |
| 设备 USB 侧假死：屏还亮，串口一个字节不来 | 先 `pnputil /scan-devices`，再拔插；relay 空闲 60 s 无字节会自动重开端口 | 拔插后 `serial COM5 open` → `online (paired)` |
| 端口被 Windows 停掉（设备代码 43），拔插板子无效 | ①把 **hub 的上游线**拔下 3 秒插回（等效重启 hub）②板子挪到主板直连口 ③重启。残留失败节点是 `USB\VID_0000&PID_0002` / `...0001` | 设备管理器里 `USB\VID_303A&PID_1001` 回到 OK；`relay.log` 重新 `online (paired)` |
| relay 掉线后重连 | 退避 5 / 10 / 20 / 40，之后每 60 秒一次；连续 120 秒无端口打一条 code 43 提示，不刷屏 | `serial recovered (...) after N attempt(s)`，间隔符合 5/10/20/40/60 |
| 重启 gateway 期间拔插板子，之后一直连不上 | relay 改成 75 秒窗口持续重试（间隔 2→5 秒）；设备侧也会自己重连 | 重启期间不用拔插；`relay.log` 多次 `ws ... failed (N)` 后 `connected` |
| 唤醒引擎「初始化成功但从不跑」：只有 `wake word ready` 一条 | 接缝四函数（`wake_available/pause/resume/feed`）必须定义在 `namespace hg`，写进 `hgp` 会让 core 的弱桩静默生效 | 出现 `resume: microphone started` / `resume: tap on`，`tap:` 计数持续增长 |
| 引擎在跑但 AFE 一帧都吃不到：`feeds` 不增长 | `xStreamBufferReceive` 按部分数据返回（213 样本），必须在任务里累加到 512（AFE 帧长）再 `feed()` | `feeds` 每 6.4 秒一条（约 31 帧/秒） |
| 喊词永远无命中，但音频正常 | MultiNet 不会自己读 Kconfig：必须显式调 `esp_mn_commands_update_from_sdkconfig()` | 出现 `hit: command N at NN percent` |
| 唤醒后开了回合却收到静音，说话没反应 | 曾是误报（`level_` 是百分比）；真问题是麦增益太低 + VAD 门槛太高。修法：`board.cpp` 的 moji 分支设 `mic_gain_db=30`、`vad_min_rms=150` | 说完能出回复；`mic: peak` 随人声抬到 200+ |
| 说完话立刻回待机、屏上没报错 | 查 `platforms.gadget.extra.voice`：`false` 时插件直接回 `Voice is off…` 并 `turn_end`，不进 ASR | 改成 `true` 并重启 gateway |
| 播报结束后喊不醒（0.2 的死窗） | 0.3 已修：唤醒引擎跟着麦克风走，不再等回合落定 | 播报结束立刻喊词能命中 |
| 提醒漏响：两条 approval 相隔几秒只有一条响 | 0.3 已修：按 `request_id` 去重，不同请求都放行 | 连续两条 approval 各自出卡片与 cue |
| 温度被读成「deg C」 | 0.3 已修：口播前转成「摄氏度」，中文屏保留 `°` | 听到「二十二摄氏度」，屏幕是 `22°C` |
| 设备上电可能自己安装暂存固件并再重启一轮 | 断电前确认 `hermes gadget update` 暂存的镜像就是目标镜像（比 SHA256） | 重启后 `diag` 的 `build` 等于目标镜像 |

**明确不做**（防范围蔓延）：设备端闹钟（用 Hermes cron）、Wi-Fi 配网回归（默认 USB）、profile 切换/休息时钟/俯卧休眠
（无 RTC/IMU）、TRMNL 式服务端渲染、e-ink 省电形态、`standalone_sender_fn` 完整版（`hermes gadget show` 是轻量替代）。

**待办与进度**：`notes/待办-20261030-moji-0.3.md`（任务 A–E 逐条状态）、`notes/验收-0.3上机.md`（上机记录）、
`notes/验收-pytest分类.md`（测试基线）。

---

## 四、许可

代码与文档沿用上游的 MIT 许可（见 [LICENSE](LICENSE)）。本项目改动是在 fork 上的独立实现，
与 Nous Research 无关；"Hermes / Hermes Agent / Nous Research" 是 Nous Research 的商标，此处仅用于说明兼容性。
第三方依赖与字库授权见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 与 [NOTICE](NOTICE)。
