# 路线 B：hermes-gadget（接 Hermes Agent）

来源：`github.com/Adolanium/hermes-gadget-sdk`（MIT，社区项目，非 Nous 官方）。2026-10-22 读 README 与 `docs/supported-hardware.md`。SDK 迭代快，命令与支持表以仓库当日内容为准。

## 它是什么

按住说话 → 本机 Hermes 想 → 板子听见回答。设备端只做音频采集与显示，推理/记忆/技能全在本机 Hermes。屏幕四态：ready / listening / thinking / speaking。可把板子的 LED、传感器暴露成 agent 工具。

## 三件套

| 件 | 位置 | 作用 |
|---|---|---|
| 固件 | `firmware/`，ESP-IDF + C++17 | 设备端主程序：设备核心、UI、音频链路 |
| Python CLI | `python/hermes_gadget`，命令 `hermes-gadget` | `build-sim` 编模拟器、`devserver` 起本地服务、`sim` 跑模拟器 |
| 插件 | `plugin/` | 装进本机 Hermes，负责配对与协议桥 |

## 第一步：桌面模拟器（不需要板子）

需要 Python 3.10+、CMake 3.16+、C++17 编译器。本机 Python 3.11.15 已满足，cmake 与 g++ 未装。

```bash
python -m pip install -e ".[dev]"
hermes-gadget build-sim --test
hermes-gadget devserver --pairing                                             # 终端 A
hermes-gadget sim --url ws://127.0.0.1:8765/gadget --board sim-466x466-round   # 终端 B
# 终端 A 输入 approve <设备屏幕上的码>
```

模拟器跑通后再接板：链路（插件 → devserver → 设备）与固件是同一套 device core，先验协议再验硬件。

## 支持表快照（2026-10-22，判断「能不能直接烧」用）

- **ESP32-S3**：微雪 ESP32-S3-LCD-1.54、Touch-AMOLED-1.75 / 1.75C / 1.8(V2) / 2.06(watch) / 2.16、Touch-LCD-1.85C V2；乐鑫 ESP-VoCat v1.2、ESP32-S3-BOX-3；M5Stack CoreS3；Spotpear ESP32-S3-1.28-BOX、ESP32-S3-1.54 muma；Xorigin AIPI Lite；LilyGO T-Display-S3；Elecrow CrowPanel 2.1 / 5.0（后两款无板载麦克风与喇叭，只能文本）
- **ESP32-P4**：Elecrow CrowPanel Advanced 10.1
- **树莓派**：Pi 4/5 的 Linux 客户端（64 位 Raspberry Pi OS Lite Trixie）
- 生命周期：所有硬件 port 在 `docs/hardware-validation.md` 有完整实物验证报告前都算 experimental；CI 只验软件行为，不验实物

**本板 `movecall-moji-esp32s3` 不在表内 → 必须自己 port。**

## port 要准备什么

1. 读 `docs/porting.md` + `docs/hardware.md`：一个 port 要提供显示驱动、音频 codec、按键与供电控制。
2. 选起点：本板是 240×240 GC9A01 + ES8311，与 Spotpear 1.28-BOX、微雪 LCD-1.54 都不同驱动，只能参考结构、不能照抄 profile。
3. 新板型不进已发布版本的浏览器安装器，期间只能源码构建（`docs/development.md`）。

## 本板的三个硬差异

| 维度 | hermes-gadget 默认 | 本板 |
|---|---|---|
| 屏 | 默认 profile 是 466×466 圆屏 | 240×240 GC9A01 |
| 音频 | 按 profile 声明 | 24kHz 固定（板级 config.h 写死） |
| 协议 | — | 采样率不对齐会变调变速 |

## 语音前提

真实对话需要本机 Hermes Agent + Gadget 插件；语音还要在 Hermes 侧配好语音识别与 TTS。缺这些时 `devserver` 只给脚本化回复（echo）。**不含唤醒词**，交互是按住说话。

## 待定（实测后写回）

- [ ] SDK 当日版本与 porting.md 的实际要求
- [ ] 模拟器在本机（WSL）跑通与否：WebSocket + 音频设备在 WSL 的可用性待验证
- [x] 本板 port 的起点选择 → 见下节「port 工作量评估」

## port 工作量评估（2026-10-23，读本地 fork 实测）

**结论：属 porting.md 里「用现有驱动的板」最简单档，零驱动代码，改 4 处配置；唯一不确定项是 GC9A01 默认初始化表能否点亮本板屏。**

| 要改 | 文件 | 改动量 |
|---|---|---|
| 板型选项 | `firmware/esp32/main/Kconfig.projbuild` | +4 行（仿 `HG_BOARD_SPOTPEAR_BOX128`） |
| 板名映射 | `firmware/esp32/main/board.cpp:25-45` | +2 行（`#elif` + `#define HG_BOARD_NAME`） |
| 板配置 `make()` | `firmware/esp32/main/board.cpp:688` 起（仿 spotpear 128 段） | +30 行 |
| 构建配置 | `firmware/esp32/boards/movecall-moji-esp32s3/sdkconfig.defaults` | 新文件约 18 行（抄 spotpear 128，改板名宏） |
| 安装器条目 | 同目录 `board.json` | 新文件 5 行 |

**不用写的部分（已核实是共享实现）**

- 显示：`SpiDisplay` 已支持 GC9A01（`port_display.cpp:211-236`），只需 `controller=Gc9a01, round=true, bgr=true, invert=true`
- 音频：单 ES8311 双向 + 模拟麦 = 官方 shared 路径（`port_codec.cpp:106-107,162`），设 `mic=Es8311` 即走 `es8311 analog` 分支
- 采样率：`DeviceProfile` 声明，服务端重采样到 speaker rate，24 kHz 不用改主机
- 无电池、无触摸：不启用 `latch_power`、`touch.enabled=false`，`Power` 各测量报 null
- 模拟器：`runner.py:52` 已有 `sim-240x240-round`，同尺寸圆屏，UI 先在桌面调

**本板 make() 目标值（照 xiaozhi config.h 填）**

```
lcd:   Gc9a01, 240x240, round=true, mirror_x=true, mirror_y=false, swap_xy=false,
       invert=true, bgr=true, mosi=17, sclk=16, cs=15, dc=7, rst=18,
       backlight=3, backlight_invert=false, 40 MHz
codec: mclk=6, bclk=14, ws=12, dout=11, din=13, pa=9, mic=Es8311, speaker=Es8311
i2c:   sda=5, scl=4, 400 kHz
buttons: talk=0（BOOT，active low）
touch.enabled=false；latch_power 不启用
```

**三个风险**

1. GC9A01 默认表不亮或花屏 → 加 17 行面板表（照 `panel_spotpear128.hpp`）+ `port_display.cpp` 一个分支；必须真机看，不能纸上定
2. 只有一个键 → 只能映射 `Talk`；`Cancel` 要在 port 里自己写长按（porting.md「A single button」条）
3. `sdkconfig.defaults` 必须含 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`：本板只有内置 USB-JTAG，用默认 UART 控制台会收不到命令

**验证顺序**：sim 跑通链路 → `idf.py build` → Windows 侧烧录 → `scripts/com5_probe.ps1` 抓板级日志 → 按键说话链路 → 按 `docs/hardware-validation.md` 记实物结果

