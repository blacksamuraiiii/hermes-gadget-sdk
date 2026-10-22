# 项目笔记：202610 hermesGadget（墨迹板 × Hermes Agent）

本目录是**我自己的项目笔记**，与上游 SDK 的 `docs/` 分开存放——上游 `docs/` 只放 SDK 官方文档，`notes/` 只放本项目记录。这样 `git pull upstream` 不会与笔记冲突。

## 仓库关系

| 角色 | 仓库 | 用途 |
|---|---|---|
| origin | `github.com/blacksamuraiiii/hermes-gadget-sdk` | 我的 fork，日常提交与推送目标 |
| upstream | `github.com/Adolanium/hermes-gadget-sdk` | 上游主仓库，只读，定期拉取 |

同步上游：

```bash
git fetch upstream
git merge upstream/main        # 或 rebase，按冲突情况选
git push origin main
```

## 当前状态（2026-10-22）

**底座已搭，未开工。** 板子未到货；工具链未安装（ESP-IDF / esptool / cmake / g++ 均无）。

## 硬件

- 板型号 `movecall-moji-esp32s3`（Movecall 墨迹板，小智 AI 衍生版）
- GC9A01 240×240 圆形 SPI 屏；ES8311 codec，24kHz
- 全部引脚与硬参 → `notes/硬件-墨迹板参数.md`

## 两条路线

| 路线 | 状态 | 说明 |
|---|---|---|
| A. 小智固件（xiaozhi-esp32） | 官方支持本板，最短路上手 | 先验硬件 → `notes/路线-小智固件.md` |
| B. hermes-gadget（本仓库） | **本板不在支持表，需自己 port** | → `notes/路线-hermes-gadget.md` |

## 关键速记（动手前必看）

- **本板不在 hermes-gadget 支持表**；同为 1.28 寸圆屏的 Spotpear ESP32-S3-1.28-BOX，驱动与引脚都不同，不能照抄 profile。
- **采样率 24kHz 写死在板级 config.h**，照抄 16kHz 会变调变速。
- **屏是 SPI 不是 I2C**，40MHz；排线长会花屏，改线先降 20MHz 验证。
- **WSL 看不到 USB 串口**，刷写走 Windows 侧工具或先做 USB 透传，别按 Linux 习惯承诺能刷。
- 板子到手先跑路线 A 确认屏/麦/喇叭都好，再动路线 B 的 port。

## 笔记索引

| 文件 | 内容 |
|---|---|
| `notes/硬件-墨迹板参数.md` | 引脚表、屏与音频参数、编译选板项（取自 xiaozhi-esp32 板级源码） |
| `notes/路线-小智固件.md` | 编译、烧录、配网步骤与验收判据 |
| `notes/路线-hermes-gadget.md` | 三件套、模拟器起步、支持表快照、port 差异点 |
| `notes/决策记录.md` | 为什么先模拟器 / 为什么现在不装工具链 / 悬而未决项 |

## 待办

1. [ ] 板子到货 → 跑路线 A 验证硬件（屏 / 麦 / 喇叭 / 背光）
2. [ ] 定路线 B 的 port 起点（要读 `docs/porting.md` 与同族板 profile 后才能定）
3. [ ] 装工具链（模拟器需 cmake + g++；自编译固件需 ESP-IDF）
4. [ ] 先跑桌面模拟器整条链路（Hermes 插件 → devserver → sim），再碰真板
5. [ ] 笔记按主题补：实测串口名、Flash 容量、配网方式、出厂固件备份
