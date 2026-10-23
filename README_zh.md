# Hermes Gadget 中文说明（第一阶段成果）

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
- 镜像：app `1,405,952` 字节，app 槽 `0x1f0000` 余 31%。

### 4. 语音入口暂时下线

`platforms.gadget.extra.voice=false` 后，按住 BOOT 只回一句屏显提示
`Voice is off. This screen only shows reminders.`，**不启动回合**（`plugin/adapter.py`
的 `on_utterance` 在 `_voice_on` 为假时直接 `send_reply` + `turn_end`）。

原因：设备转写经常为空，空转写照样起回合（`inbound message: platform=gadget ... msg=''`），
一轮挂过 30 分钟以上。提醒屏不需要录音链路。要恢复：把 `voice` 设回 `true` 并重启 gateway。

### 5. `standalone_sender_fn`：搁置（阶段一不做）

现状：进程外 `hermes send --to gadget:<id> "文本"` 与进程外 `cron --deliver gadget:` 仍报
`missing standalone_sender_fn`。三条候选投递路径（spool 轮询 / hub 加本地 HTTP 收口 /
hub 开 operator WS 角色）比较过、未落地，决定先不做。

---

## 二、跑起来要几步（本机实测顺序）

1. **刷固件**：用 [firmware/releases/v0.1/](firmware/releases/v0.1/) 里的 bin 直刷，命令与地址见那里的 README。
2. **装插件**：把 `plugin/` 放到 `~/.hermes/plugins/gadget`（复制或符号链接）。
3. **写配置**（`platforms.gadget`）：`enabled: true`、`home_channel`（设备 id），
   `extra` 里 `voice: false`、`relay_start` / `relay_stop` 指向 `tools/relay_start.ps1` / `relay_stop.ps1`。
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
| 语音已下线 | 屏显 `Voice is off...`，且 `gateway.log` 无 `platform=gadget` 的 inbound、无空回合 |
| 字号 | `gadget_devices` 上报 `text_cols=26, text_rows=5` |

---

## 四、已知边界与坑

- GB2312 一级以外的罕字显示 `?`；升级二级字库需 +60% 空间（app 槽尚有余量）。
- 打开 COM5 会复位设备，启动头 2–4 秒内发 console 命令会回 `@error ESP_ERR_INVALID_STATE`，等启动完再发。
- console 命令与 relay 抢同一个串口：先停 relay，发完再起（`tools/console_cmd.py` 会自动让开再恢复）。
- 设备 USB 侧会假死（屏还亮、串口一个字节不来）：先 `pnputil /scan-devices`，再拔插；看门狗会在 120 s 内重启 relay。
- 关机流程里 `/mnt/c` 已卸载，`relay_stop` 会报 `powershell.exe: not found`——属瞬态，重启后 gateway 会重新拉起。
- 设备上电可能自己安装暂存固件并再重启一轮，收尾前确认暂存的就是想要的镜像。

---

## 五、下一步

- **提示词唤醒**：让屏在人说话时被动唤醒（上游 README 明确 wake-word 未包含），计划先做关键词唤醒而非完整唤醒词引擎。
- 进程外投递（`standalone_sender_fn` + spool）：搁置，等需求明确再做。

计划与决策记录在 [notes/决策记录.md](notes/决策记录.md)；硬件参数、路线对比在 `notes/` 下。

---

## 六、许可与上游关系

代码与文档沿用上流的 MIT 许可（见 [LICENSE](LICENSE)）。本项目改动是在 fork 上的独立实现，
与 Nous Research 无关；"Hermes / Hermes Agent / Nous Research" 是 Nous Research 的商标，
此处仅用于说明兼容性。第三方依赖与字库授权见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 与 [NOTICE](NOTICE)。
