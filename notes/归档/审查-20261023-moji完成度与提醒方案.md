# 审查：Moji port 完成度 + 桌面提醒复用方案

- 仓库：`202610hermesGadget`（`Adolanium/hermes-gadget-sdk` 本机 fork，`main` + 未提交改动）
- 目标板：`movecall-moji-esp32s3`（ESP32-S3-WROOM-1-N16R8 / GC9A01 240×240 / ES8311）
- 快照：2026-10-23 11:5x（本机 git 工作区，非远端 HEAD）
- 证据：`[码]` 源码 · `[文]` 仓库文档 · `[厂]` 上游/厂商文档 · `[测]` 本机实测 · `[推]` 推断 · `[?]` 缺失

---

## 一、完成度对照

| 预期功能                                                                                       | 状态                         | 依据                                                                                | 缺口                              |
| ------------------------------------------------------------------------------------------ | -------------------------- | --------------------------------------------------------------------------------- | ------------------------------- |
| 板级四件套（`board.cpp` / Kconfig / `sdkconfig.defaults` / `board.json`）                         | 已完成                        | `board.cpp:727-761`、`Kconfig.projbuild:112`、`boards/movecall-moji-esp32s3/` `[码]` | 无                               |
| PlatformIO 环境                                                                              | 已完成，未编译验证                  | `platformio.ini:151-157`（`[env:movecall-moji]`，16 MB）`[码]`                        | 本机无 ESP-IDF / pio，未跑过 `pio run` |
| 文档五处（hardware / supported-hardware / hardware-validation / CHANGELOG / NOTICE+THIRD_PARTY） | 已完成，含 2 处事实错误              | `docs/hardware.md:15-65` 等 `[码]`                                                  | 见 P2、P3                         |
| 实机：启动、屏初始化、ES8311 双向、自报板名                                                                  | 已完成                        | 串口日志、`notes/硬件-墨迹板参数.md:51` `[测]`                                                 | 无                               |
| 实机：屏幕画面 / 按住说话 / 配网 / 配对 / 完整语音一轮                                                          | 未完成                        | `docs/hardware-validation.md:27` 自述 `[文]`                                         | 检查清单 5 项未销账                     |
| 计划任务 A：PSRAM 静默回退可观测性                                                                      | 未做                         | `port_display.cpp` / `port_diag.cpp` 无改动 `[码]`                                    | 仍是"内部 RAM 静默降级"                 |
| 计划任务 C：USB 串口隧道                                                                            | 代码完成，实机未验                  | `port_serial.cpp`(303 行)、`usb_relay.py`(407 行) `[码]`；relay 自检 6/6 通过 `[测]`        | 无 pytest、无文档小节、并发写帧有缺陷（P1）      |
| 模拟器 profile                                                                                | 未做（复用 `sim-240x240-round`） | `CHANGELOG.md:19` `[码]`                                                           | 可选，不阻断                          |
| 桌面提醒 / 常驻信息屏                                                                               | 未做                         | 无对应代码路径 `[码]`                                                                     | 见第三部分                           |

**一句话结论**：板级 port 与文档闭环基本完成，卡在"实机五项验证"；USB 直连是新写的、未上板验证的链路，且带一个并发缺陷；PSRAM 可观测性这一项被跳过了。

---

## 二、问题清单（按阻断级排序）

### P0-1 `port_serial.cpp` 无条件编译，可能拖垮其余 19 块板

- 现象：`CMakeLists.txt:36` 无条件加入 `port_serial.cpp`，而该文件 `:136` 使用 `CONFIG_HG_LINK_USB_PATH`；该 Kconfig 是 `string` 且 `depends on HG_LINK_USB_SERIAL`（`Kconfig.projbuild:9-20`）。其余板 `HG_LINK_USB_SERIAL=n`，`CONFIG_HG_LINK_USB_PATH` 很可能不进 `sdkconfig.h`。
- 对比：`port_ble.cpp` 全文包在 `#if CONFIG_BT_NIMBLE_ENABLED` 里，是本仓库既有的条件编译惯例。
- 定性：`[推]` 高概率编译失败，`[?]` 未实测。
- 修法：给 `port_serial.cpp` 全文加 `#if CONFIG_HG_LINK_USB_SERIAL #endif`，或 CMake 里条件加入。

### P0-2 帧写入没有互斥：控制帧、音频帧、日志会互相穿插

- `write_frame()`（`port_serial.cpp:67-96`）按 64 B 分块 flush，一次音频帧 1920 B ≈ 30 次写；`tx_task`（`:180`）与 `rx_task`（ping 回声 `:275`、console 回复 `printf` `:58`）都直接写同一个 USB 端点，中间可被插入。
- 后果：relay 侧 payload 被污染——控制帧 JSON 解析失败、音频帧变成噪声；且 `0xC0` 定界的帧一旦被日志文字插入，长度与内容都不对。
- 修法（二选一）：
  1. 整帧先编码进 4 KB（音频 1920→最多 3840 B）缓冲，单次 `usb_serial_jtag_write_bytes` 写出；
  2. 用一把 mutex，同时用 `esp_log_set_vprintf` 把日志也接到同一把锁后。

### P1-1 板级默认值把这块板做成"无 Wi-Fi 设备"，文档仍按 Wi-Fi 写

- `boards/movecall-moji-esp32s3/sdkconfig.defaults` 里 `CONFIG_HG_LINK_USB_SERIAL=y`、`CONFIG_HG_DEFAULT_SERVER_URL="ws://usb/gadget"` `[码]`；`port_wifi.cpp:231-240` 在该模式下不启动配网 AP，`main.cpp:169-200` 屏蔽 Wi-Fi 的 NetDown。
- 但 `docs/hardware-validation.md:27`、`CHANGELOG.md:19` 仍写"Wi-Fi setup … not verified"，`docs/hardware.md` 的板节也没提 USB 链路；`docs/setup-board.md` 的手机配网流程对这块板不成立。
- 定性：`[码]` 事实矛盾。发布镜像若带此默认值，用户按文档配网必然失败。
- 修法：要么默认值改 `n`（保留 Wi-Fi 路径，USB 链路作为可选项），要么补 `USB link` 小节并改掉这 2 处措辞。

### P1-2 文档锚点写错（CI 链接检查会挂）

- `docs/supported-hardware.md:23` 链接 `hardware.md#movecall-moji-esp32s3`；实际标题是 `## Movecall Moji (ESP32-S3)`，GitHub 锚点为 `#movecall-moji-esp32-s3`。`board.json` 里写的是对的。
- 修法：补一个连字符。

### P1-3 设备侧事件通道是死代码

- `App::emit_event()`（`app.cpp:1232`）全仓只有定义、无调用 `[码]`；协议文档里的 `{"type":"event","notify":true}`（`docs/protocol.md:220`）因此永远发不出去。
- 影响：想用"按键/传感器触发 → 送进对话"这条现成通路做提醒，目前没有接线。

### P2-1 relay 缺三项工程化

- 无 pytest 覆盖：`tests/` 里搜不到 `usb_relay`，CI 不会跑 `--selftest` `[码]`。
- 无 stdin 转发：跑着 relay 时无法向设备敲 `cancel` / `diag`（`diag` 只能靠设备自己打印）。
- `--reset` 注释说"在 Moji 上会让板子静默"（`usb_relay.py:376-378`），属于未解释的坑。

### P2-2 帧上限与 USB 发送缓冲不匹配

- `kMaxFrame = 16 KB`（`port_serial.cpp:42`），但 IDF 端 TX 环只有 4096 B（`:104`），分块写 100 ms/次；一个 16 KB 帧最坏 25 s，会撞上 `kStuckMs=60 s` 之外的心跳与串口输出堵塞。实际音频帧 1920 B 无碍，属于"参数不自洽"。

### P2-3 计划任务 A 未做（PSRAM 静默降级）

- `port_display.cpp` 回退到内部 RAM 时无日志，`CONFIG_SPIRAM_IGNORE_NOTFOUND=y` 让故障不可见；排障会被 `hardware.md` 的"黑屏→面板表/背光"结论带偏。

---

## 三、提醒能力：社区与现有实现盘点

| 实现                                                               | 机制                                                                 | 能否直接复用              | 结论                                                                                 |
| ---------------------------------------------------------------- | ------------------------------------------------------------------ | ------------------- | ---------------------------------------------------------------------------------- |
| **Hermes cron → gadget** `[码][文]`                                | `hermes cron create "30m" "…" --deliver gadget:<id>`，走适配器投递成一"条消息" | ✅ 零代码               | **首选**。本仓库 `docs/hermes-integration.md:100-104` 已写明；回复类消息不受"熄屏暂存"影响，会点亮屏幕并 TTS     |
| `hermes send --to gadget:<id>` `[厂][推]`                          | 不经 LLM 的直接投递                                                       | ✅ 零代码（需 gateway 在跑） | 适合脚本/CI：`echo 该喝水了 \| hermes send --to gadget:hg-xxxx`                             |
| `gadget_display` 工具 `[码]`                                        | agent 调 `display` 卡片（title/body/seconds）                           | ✅ 已有                | 只在"有一轮对话"时可用，不能定时；`seconds=0` 常驻                                                   |
| dashboard + `hermes gadget tile` `[码]`                           | 会话状态 + 状态瓦片，2 s 轮询文件                                               | ⚠️ 本板不可用            | `caps.dashboard` 需要 `has_cancel_button \|\| touch_screen`（`app.cpp:392`），Moji 两者皆无 |
| Hermes 网关事件钩子 `HOOK.yaml` `[厂]`                                  | `agent:start/step/end`、`session:end` 等事件跑 `handler.py`             | ⚠️ 需一个落点            | 钩子本身现成，缺"钩子→设备消息"的桥                                                                |
| 插件 hook（`dashboard.SessionBoard`）`[码]`                           | `pre_llm_call`/`on_session_end`/`pre_approval_request`             | ✅ 数据已在              | 现在只喂 dashboard；对无 dashboard 的板没有消费者                                                |
| xiaozhi-alarm-mcp `[厂]`                                          | 设备端闹钟：`Alarm{time,name,repeat,interval}` + NVS + MCP 工具            | ❌ 不可直接移植            | 它是小智 MCP 生态的固件分支；本仓库无 MCP、无本地时间基准。可借数据结构，不借代码                                      |
| AI 状态灯类项目（AI-status-light / AgentLight / claude-code-light）`[厂]` | 主机 hook → USB 串口/BLE → 灯                                           | ❌ 固件不能复用            | 验证了"hook→串口→设备"这条链可行；本仓库的 relay 已是同构实现，无需再引一套 daemon                               |
| TRMNL / LaMetric / Divoom `[厂]`                                  | 服务端渲染整屏后推送                                                         | ❌                   | 需要设备侧 HTTP 拉取/渲染，等于重做 UI 栈，收益低                                                     |

**关键判断**：提醒所需的三块能力（定时、免 LLM 投递、屏上呈现）在 Hermes 侧都已存在，缺的只是"提醒文案 → 一块没有取消键的板子"这一段接线，不需要新造调度器或新协议。

---

## 四、推荐落地方案（按投入排序）

### L0 零代码：先用 cron 投递（当天可验证）

```bash
# 一次性提醒（30 分钟后），交给 agent 组织语言 → 会 TTS + 上屏
hermes cron create "30m" "提醒我：泡茶，然后回来继续写 port 文档" --deliver gadget:hg-xxxxxxxx

# 周期提醒
hermes cron create "0 9 * * 1-5" "提醒我：今天的站会 10 分钟前准备" --deliver gadget:hg-xxxxxxxx

# 零 token：脚本 stdout 原样投递（适合固定文案）
hermes cron create "every 45m" --no-agent --script water.sh --deliver gadget:hg-xxxxxxxx
```

前提：`hermes gadget devices` 能看到设备且 `paired`；设备在线（USB 模式下需 `tools/usb_relay.py` 在跑）。

### L1 插件小补丁（约 80 行）：`hermes gadget show`

- 复用 `dashboard.TileStore` 的"文件 + 原子替换 + expires"模式新增 `card.json`；
- 复用 `adapter._watch_dashboard()`（`adapter.py:351`，2 s 轮询）读它，调现成的 `session.show_card()`（`hub.py:357`）；
- 收益：任何脚本/钩子都能推卡片，不产生会话、不烧 token，`ttl_s` 自动消失——正好匹配"不需应答的桌面提醒"。

### L2 钩子消费者（约 20 行）：把"等待批准"变成可见提醒

- `adapter._board_changed` 已有全部会话状态；对 `caps.dashboard == 0` 的设备（Moji），在状态变为 `waiting` 时推一张 `display` 卡（或 `notice`），持续到状态离开 `waiting`。
- 这条直接对上"对话 hook 消息系统"：钩子早就在跑，只是没有出口。

### L3 硬件（一句话改动，解决"BOOT 难按"）：外接一个取消键

- Moji 已用：0/3/4/5/6/7/9/11-18/21；保留：19-20(USB)、26-37(Flash/PSRAM)、39-42(JTAG)、45-46(strapping)。**GPIO 10（或 8）空闲** `[码][推]`。
- 改动：`board.cpp:759` → `b.buttons = {0, 10, -1, -1};`（`talk, cancel, up, down`，`port_buttons.cpp:21` 已开内部上拉，按键对 GND）。
- 附带解锁：`has_cancel_button` 变真 → `caps.dashboard` 上报 → 现有**会话仪表盘**（`docs/protocol.md:176`）在这块板上从"不可用"变"可用"，等于白得一个桌面状态屏。
- 代价：一次焊接 + 外壳开孔；需确认 GPIO10 在 PCB 上有可焊点 `[?]`。

### L4 不建议：设备端闹钟

需要 SNTP 对时、NVS 闹钟表、本地提示音播放、以及与云端提醒去重；Hermes 侧 cron 已经免费提供这些，属于重复造轮子。除非要"断网也响"。

---

## 五、验证清单（改完按序执行）

| # | 检查             | 判据                                                                                                  |
| - | -------------- | --------------------------------------------------------------------------------------------------- |
| 1 | 其余板未被 USB 链路拖死 | `cd firmware/esp32 && pio run -e esp32s3-breadboard` 通过                                             |
| 2 | 本板编译           | `pio run -e movecall-moji` 通过，`check_config.py` 不报未生效行                                              |
| 3 | relay 自检       | `python tools/usb_relay.py --selftest` → `all checks passed`（本机已过）`[测]`                             |
| 4 | 链路建立           | 设备日志 `USB link ready` → `asked the relay for /gadget` → `relay connected`；relay 侧 `[dev] hello ack` |
| 5 | console 未破     | relay 日志里出现 `parts: display gc9a01, microphone es8311, speaker es8311`                              |
| 6 | 提醒端到端          | `hermes cron create "2m" "提醒测试" --deliver gadget:<id>` → 2 分钟内屏幕出现文本、喇叭出声                           |
| 7 | 熄屏后的提醒         | `set screen_timeout 60` 等屏灭，再投一次提醒 → 消息应点亮屏幕（卡片则会被暂存，属预期）                                           |
| 8 | 外接键（若做 L3）     | `diag` 报 `key: true`；短按取消当前卡片；短按空闲进入 dashboard                                                      |

---

## 六、风险与未验证项

1. `[?]` `CONFIG_HG_LINK_USB_PATH` 在其它板的 `sdkconfig.h` 里是否存在——决定 P0-1 是"必然挂"还是"侥幸过"；必须用一次 breadboard 构建确认。
2. `[?]` USB-Serial/JTAG 在 console 已初始化后再 `usb_serial_jtag_driver_install()`，输出是否仍然稳定（本板尚未实机跑过 relay）。
3. `[?]` 4 KB 编码缓冲 + 单次写在 1920 B 音频帧下的最坏阻塞时间。
4. `[推]` `hermes send --to gadget:<id>` 在 gadget 平台需要 gateway 在跑（它没有 bot token，无法脱离 gateway 直发）。
5. `[推]` `--no-agent` 的 cron 脚本输出经 gadget 适配器投递时是否触发 TTS，未在真机确认。
6. `[?]` GPIO10 在该 PCB 上是否有可用焊盘；若无，退到 GPIO8 / 47 / 48。
7. `[?]` 若保留 `CONFIG_HG_LINK_USB_SERIAL=y` 作为发布默认值，需同时接受"这块板不支持 Wi-Fi 配网"，并同步改掉 `hardware-validation.md:27`、`CHANGELOG.md:19`。
