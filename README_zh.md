# Hermes Gadget 中文说明（moji-0.2：语音唤醒打通）

本仓库是 [Adolanium/hermes-gadget-sdk](https://github.com/Adolanium/hermes-gadget-sdk) 的 fork，
把一块 ESP32-S3 圆屏小板（`movecall-moji-esp32s3`，墨迹板）改造成 **Hermes Agent 的消息提醒屏**：
不需要按键说话，任务需要人介入或跑完时，屏幕出卡片、喇叭出提示音。

上游英文文档见 [README.md](README.md) 与 [docs/](docs/)；本文件只写本项目第一阶段的改动、用法与边界。

---

## 一、第一阶段做了什么（5 项）

### 1. USB 线直连，跳过 Wi-Fi 配网

链路：板子原生 USB Serial/JTAG → Windows 侧 `tools/usb_relay.py` 转发 → gateway 内 hub（`127.0.0.1:8765/gadget`）。

固件侧两条配置（`firmware/esp32/boards/movecall-moji-esp32s3/sdkconfig.defaults`）：

```
CONFIG_HG_LINK_USB_SERIAL=y
CONFIG_HG_DEFAULT_SERVER_URL="ws://usb/gadget"
```

好处：不用配网、不依赖 2.4 GHz 路由器、换房间不掉线。代价：必须插 USB 线，且 relay 必须在跑。
要回到 Wi-Fi 传输，注释掉 `CONFIG_HG_LINK_USB_SERIAL` 重编即可，代码不用改。

### 2. 三类消息提醒 + 消息网关 + relay 随 gateway 起停

提醒来自 Hermes 的插件钩子，不轮询会话状态：

| 时机 | 钩子 | 表现 |
|---|---|---|
| 命令等待授权 / 等待口令 | `on_human_input_request`（kind=`approval`/`sudo`） | 播 cue，卡片 `APPROVAL` / `PASSWORD` |
| 等你做选择（clarify） | `on_human_input_request`（kind=`clarify`） | 播 cue，卡片 `QUESTION` |
| 长回合结束（默认 >45 s） | `on_session_end` | 播 cue，卡片 `DONE` |

- cue 是 `tools/make_cues.py` 预生成的 3 个 wav（`approval` / `clarify` / `done`，`sudo` 复用 `approval`），
  放在 `~/.hermes/plugin-data/gadget/cues/`。整条提醒不起回合、不调 STT/TTS、不花 token。
- 设备声明 `charset=utf-8` 时卡片写中文（需要授权 / 需要口令 / 请选择 / 已完成）。
- relay 生命周期挂在 gateway 上：插件 `extra.relay_start` 在 `connect()` 拉起、`extra.relay_stop` 在
  `disconnect()` 停下；另有 120 s 看门狗兜崩溃与拔插。Windows 侧用 `pythonw.exe` 起，**没有窗口**。
- 开关：`platforms.gadget.extra.reminders`（默认 true）、`reminder_min_turn_s`（默认 45）。

### 3. 屏幕字体：中文 CJK 点阵 + 12 px

- 字库：Fusion Pixel 12px 等宽 zh_hans（仓库 MIT / 包内 OFL 1.1），GB2312 一级 3755 字 + 6×12 ASCII。
- 版面：ASCII 占 1 列、汉字占 2 列，行高 14（字形 12 + 2 间距）。240 圆屏有效区 169×169 下是
  **26 列 × 5 行**，一行 13 个汉字（原 5×7 字形是 26 列 × 10 行）。
- 生成器：`tools/make_cjk_font.py`（BDF → `firmware/core/src/font_ascii.cpp` / `font_cjk.cpp`）。
- 镜像：app `2,199,840` 字节，app 槽 `0x3e0000` 余 46%（0.2 起槽放大，见下）。

### 4. 语音唤醒 + 「我在」提示音（0.2 新做）

- **唤醒词**：esp-sr MultiNet5（`mn5q8_cn`，模型在 4 MB 的 `model` 分区），两条命令词
  `ni hao xiao mo`（你好小墨，command 0）与 `xiao mo xiao mo`（小墨小墨，command 1）。
  采样路径：codec 24 kHz → 官方 `esp_audio_effects` 的 `rate_cvt` → 16 kHz → AFE 单麦 → MultiNet。
- **接缝**：core 的 `App::update_wake()` 是唯一决定谁持麦的地方。引擎不另开麦克风——
  core 的 `wake_attach(&mic)` 交出 talk 那一路，样本走音频端口的 `MicTap`（I2S 与 codec
  两条投递点都在 `events::post` 之前打 tap）。
- **命中后**：先给宿主发 `{"type":"wake.hit","command":N,"score":M}`，屏幕立刻亮，
  **不马上录音**；宿主侧播「我在」（`~/.hermes/plugin-data/gadget/cues/wake.wav`，1.22 秒），
  等 `speaking()` 转假（最多等 700 ms）才 `start_listening(true)`。
  顺序不能反：单麦无 AEC，边播边录会把提示音自己录进语音，ASR 必废。
- **提示音走宿主不做本地**（用户拍板）：改文件即换、不用重刷固件；`tools/make_cues.py` 用
  edge-tts 切，24 kHz 单声道直出，和提醒 cue 共用缓存与播放路径（`adapter.play_cue()`）。
- 开关：`platforms.gadget.extra.voice` 必须为 `true`，否则插件回一句
  `Voice is off. This screen only shows reminders.` 并立刻 `turn_end`，根本不进 ASR。
- 分区：app 涨到 2.1 MB 撑破旧槽（`0x1f0000`），0.2 把两个 ota 槽放大到 `0x3e0000`、
  `model` 上移到 `0x800000`。**从 0.1 升级必须整片刷一次**，之后 OTA 才继续可用。

### 5. `standalone_sender_fn`：搁置（阶段一不做）

现状：进程外 `hermes send --to gadget:<id> "文本"` 与进程外 `cron --deliver gadget:` 仍报
`missing standalone_sender_fn`。三条候选投递路径（spool 轮询 / hub 加本地 HTTP 收口 /
hub 开 operator WS 角色）比较过、未落地，决定先不做。

---

## 二、跑起来要几步（本机实测顺序）

1. **刷固件**：用 [firmware/releases/v0.2/](firmware/releases/v0.2/) 里的 bin 整片刷（含
   `srmodels.bin` @`0x800000`），命令与地址见那里的 README。
2. **装插件**：把 `plugin/` 放到 `~/.hermes/plugins/gadget`（复制或符号链接）。
3. **写配置**（`platforms.gadget`）：`enabled: true`、`home_channel`（设备 id），
   `extra` 里 `voice: true`（唤醒后要能对话）、`relay_start` / `relay_stop` 指向
   `tools/relay_start.ps1` / `relay_stop.ps1`。提示音文件用 `hpy tools/make_cues.py` 生成。
4. **起 relay**：WSL 侧 `tools/relay.sh start`（无窗口），或让 gateway 自己拉。
5. **看判据**：`tools/relay.log` 出现 `serial COM5 open` 与 `hg: online (paired)` 即链路通。

`tools/relay_start.ps1` 的 Python 解释器取 `RELAY_PYTHONW` 环境变量，
否则找用户目录下名为 `gadget-relay` 的 conda 环境（该环境需有 `pyserial` 与 `websockets`）。
`tools/relay.sh` 同理认 `RELAY_ENV_DIR`。

---

## 三、验收判据（都靠设备自报，不靠肉眼）

| 项 | 判据 |
|---|---|
| 链路 | `tools/relay.log`：`serial COM5 open` → `hg: online (paired)` |
| 提醒出声 | `hg.codec: playback ended ... N B played`，N 与对应 cue 的 wav 字节数一致 |
| 卡片 | 下行 `display {"title":...}`；CJK 固件上标题为中文 |
| 熄屏后提醒 | 设备自报 `state display_sleeping:true`，播报时回 `false` 并出卡片 |
| 唤醒命中 | `hg.wake: hit: command 0 at NN percent` + `hg: wake: ...`，屏幕进 Listening |
| 「我在」播出 | `hg.codec: playback ended ... 58752 B played`（与 `wake.wav` 字节对应），且**播完才**出现录音帧 |
| 唤醒后能对话 | 说完一句后 `hg: recording: end, N chunks`，宿主出回复；不再出现 `recording: still silent` |
| 引擎在持麦 | 空闲时每 200 次 tap 一条 `hg.wake: tap: ... peak N`，`peak` 随人声起伏（不是恒 0） |
| 空转写/静音不启回合 | 按住不到 0.5 s 或录到的是房间噪声时，设备收到 `Didn't catch that - hold the button while speaking`，`gateway.log` 无对应 inbound |
| console 命令让路 | 停 relay 由 `tools/console_cmd.py` 自动处理（先停、发完再起）；判据是回复以 `@` 开头（`@ok` / `@value` / `@error`），期间无 `PermissionError(13)` |
| 字号 | `gadget_devices` 上报 `text_cols=26, text_rows=5` |

---

## 四、已知边界与坑

每条按「现象 / 处理 / 验收」写，不留纯描述。

| 现象 | 处理 | 验收 |
|---|---|---|
| 罕字显示 `?`（GB2312 一级以外） | 字库已扩到 GB2312 全表：实收 6647 字（一级 3755 全覆盖、二级 2892/3008）；剩余缺口是源 BDF 本身没有这 116 个二级字 | 卡片里实际出现的罕字不再显示 `?`；`tools/make_cjk_font.py` 会打印缺字清单 |
| 打开 COM5 会复位设备，启动头 2–4 秒内发 console 命令回 `@error ESP_ERR_INVALID_STATE` | `tools/console_cmd.py` 默认 `--wait-ready`：等启动行再发，命中该错误自动重试 3 次；`--no-wait-ready` 才直发 | 命令输出 `@ok` / `@value`，输出里不再出现 `ESP_ERR_INVALID_STATE` |
| console 命令与 relay 抢同一个串口 | `console_cmd.py` 先停 relay，发完再起（无需手工 `taskkill`） | 发命令期间无 `PermissionError(13)`；发完 `relay.log` 重新出现 `hg: online (paired)` |
| relay 不支持从 stdin 直接敲命令（裸字节插进 SLIP 帧中间把帧打坏） | 发 console 命令一律走 `tools/console_cmd.py`；relay 只负责帧转发 | 链路日志里没有坏帧/JSON 解析失败；console 回复都能拿到 `@` 开头的单行 |
| `tools/usb_relay.py --reset` 在本板上让板子静默（RTS 复位后 usb-serial-jtag 不再应答） | 不要用 `--reset`，默认连接正在跑的设备；需要重启就断电或复位整板 | 用了会看到 relay 反复 `serial reopen failed (port missing)`，恢复要拔插或复位 hub —— 所以文档与 `--help` 都写明不用它 |
| 设备 USB 侧假死：屏还亮，串口一个字节不来 | 先 `pnputil /scan-devices`，再拔插 USB 线；relay 空闲 60 s 无字节会自动重开端口 | 假死期间 `relay.log` 每 5–60 s 出现 `serial reopen failed (port missing)` 并计数；拔插后 `serial COM5 open` → `hg: online (paired)` |
| 唤醒引擎"初始化成功但从不跑"：只有 `wake word ready` 一条，喊词无反应 | 接缝四函数（`wake_available/pause/resume/feed`）必须定义在 `namespace hg`；写进 `namespace hgp` 会让 core 的弱桩静默生效 | `relay.log` 出现 `resume: microphone started` / `resume: tap on`；`tap:` 计数持续增长 |
| 引擎在跑但 AFE 一帧都吃不到：`feeds` 不增长、`tap:` 正常 | `xStreamBufferCreate(16384, 2)` 的触发阈值是 2 字节，`xStreamBufferReceive` 按部分数据返回（213 样本），必须在任务里自行累加到 512（AFE 帧长）再 `feed()` | `feeds 200/400/600` 每 6.4 秒一条（约 31 帧/秒 = 16 kHz） |
| 喊词永远无命中，但音频正常 | MultiNet 的命令词不会自己去读 Kconfig：必须显式调 `esp_mn_commands_update_from_sdkconfig()`，否则模型里零短语 | 命中日志出现 `hit: command N at NN percent` |
| 唤醒后开了回合却收到静音：`recording: still silent after N chunks`，说话也没反应 | 这一条曾是误报（`level_` 是百分比，RMS 一百多取整为 0）。真问题是**麦增益太低 + VAD 门槛太高**：实测安静峰值 50–90、说话峰值最高 841，而默认 `vad_min_rms=500`，永远够不到 → 立刻 `cancel_listening("no speech")` 回待机。修法：`board.cpp` 里 moji 分支设 `mic_gain_db=30`、`vad_min_rms=150` | 说完一句能出回复；`hg.codec: mic: peak` 随人声明显抬升（说话 200+） |
| 说完话立刻回待机、但屏上没报错 | 查 `platforms.gadget.extra.voice`：为 `false` 时插件直接回 `Voice is off…` 并 `turn_end`，不进 ASR | 改成 `true` 并重启 gateway |
| 重启 gateway 期间拔插板子，之后一直连不上 | 板子起在 gateway 还没监听的那两分钟里，relay 只重试 3 次（6 秒）就回设备 `no plugin`，而设备侧不会自己再试。已把 relay 改成 75 秒窗口持续重试（间隔 2→5 秒） | 重启 gateway 期间设备自己重连，不用拔插；`relay.log` 出现 `ws ... failed (N)` 多次后 `connected` |
| 端口被 Windows 停掉（设备代码 43，「请求 USB 设备描述符失败」），拔插板子无效 | 顺序：①把 **hub 的上游线**拔下 3 秒插回（等效重启 hub，不用管理员、不用重启电脑）②把板子挪到主板直连口 ③重启。本机 4 号口（VIA hub，父设备 `USB\VID_2109&PID_2817\MSFT20`）复现过，残留失败节点是 `USB\VID_0000&PID_0002`（描述符请求失败）与 `USB\VID_0000&PID_0001`（端口重置失败） | 设备管理器里 `USB\VID_303A&PID_1001` 回到 OK、失败节点消失；`relay.log` 重新 `online (paired)` |
| relay 掉线后重连 | 退避 5 / 10 / 20 / 40，之后每 60 秒一次；连续 120 秒没有端口会打一条 code 43 提示，同一条警告不会每 5 秒刷屏 | `relay.log` 出现 `serial recovered (...) after N attempt(s)`，且阶段间隔符合 5/10/20/40/60 |
| 关机流程里 `/mnt/c` 已卸载，`relay_stop` 报 `powershell.exe: not found` | 属瞬态：插件把停 relay 的失败降级为 debug 日志，不再当错误刷屏 | 关机日志里不再出现该 WARNING；下次启动 gateway 会重新拉起 relay |
| 设备上电可能自己安装暂存固件并再重启一轮 | 收尾检查项：断电前确认 `hermes gadget update` 暂存的镜像就是目标镜像（比 SHA256） | 重启后设备自报的版本与构建时间等于目标镜像（`diag` 的 `build` 字段） |

---

## 五、下一步

- **唤醒阈值标定**：目前只有散点样本（`你好小墨` 中位 73%、`小墨小墨` 中位 46%），要按 0.5 / 1 / 2 米
  各 10 遍、安静与噪声两档采完再定 15/20/25；「小墨小墨」是可重复词，区分度天然低，考虑换成
  「小墨同学」这类不重复的说法。
- **清诊断**：`port_wake.cpp` 的 `threshold = 0.05f`（临时值）要恢复成
  `CONFIG_HG_WAKE_THRESHOLD / 100`；tap 峰值、feeds、`mic: peak`、`recording: ... silent`
  这些 bring-up 日志要删，只留 `hit:`。
- **开发板独立 profile**：`gadget` profile 已建好（模型/TTS/STT 与 default 一致、插件只留
  gadget/sensevoice/memory、技能空目录），尚待 `gateway.profile_routes` 把设备指过去并重启验证。
- 进程外投递（`standalone_sender_fn` + spool）：搁置，等需求明确再做。

计划、决策与实测记录在 [notes/](notes/README.md)：现状入口 `notes/README.md`，决策依据 `notes/决策-台账.md`，0.1 实测 `notes/验收-0.1实测记录.md`，本机环境与坑 `notes/环境-本机工具链与坑.md`。

---

## 六、许可与上游关系

代码与文档沿用上流的 MIT 许可（见 [LICENSE](LICENSE)）。本项目改动是在 fork 上的独立实现，
与 Nous Research 无关；"Hermes / Hermes Agent / Nous Research" 是 Nous Research 的商标，
此处仅用于说明兼容性。第三方依赖与字库授权见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 与 [NOTICE](NOTICE)。
