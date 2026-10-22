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
- [ ] 本板 port 的起点选择
