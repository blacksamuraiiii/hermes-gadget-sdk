# 项目笔记：202610 hermesGadget（墨迹板 × Hermes Agent）

本目录是**我自己的项目笔记**，与上游 SDK 的 `docs/` 分开存放——上游 `docs/` 只放 SDK 官方文档，`notes/` 只放本项目记录。这样 `git pull upstream` 不会与笔记冲突。

- 最后核对：2026-10-26
- 驱动文档：`待办-20261026-v0.2.md`（0.2 三项任务、上游分流表、待拍板事项）

## 一条线读懂现状

墨迹板已从「小智固件板」改成 **Hermes Agent 的消息提醒屏**：USB 线直连（不经 Wi-Fi），中文 12 px 点阵上屏，三类人工介入（授权 / 选择 / 完成）走插件内通知桥；语音入口主动关闭（空转写会起空回合）。

| 维度 | 现状（2026-10-26） |
|---|---|
| 固件 | v0.1 = 提交 `86381e5`；镜像 1,405,952 B（app 槽 `0x1f0000`，余 31%）；四件套 bin 在 `firmware/releases/v0.1/` |
| 链路 | USB Serial/JTAG → `tools/usb_relay.py`（Windows 侧，pythonw 无窗口）→ 插件；relay 随 gateway 成对起停 + 120 s 看门狗 |
| 屏 | 240×240 GC9A01 圆屏；12 px 字库 → 26 列 × 5 行，13 汉字/行；GB2312 一级 3755 字 |
| 提醒 | 钩子 → 通知桥：三条 cue（授权 / 提问 / 完成）+ `display` 卡片；cue 在 `~/.hermes/plugin-data/gadget/cues/` |
| 语音 | 关闭（`platforms.gadget.extra.voice=false`）；按 BOOT 只回一句屏显提示，不起回合 |
| 版本号 | fork 自编 v0.1；构建的 `PROJECT_VER` 默认仍是上游的 `0.2.0`；上游 tag 到 `v0.4.1` —— 两套编号不可混用（0.2 要收口，见待办 1A/2.1） |

## 仓库关系

| 角色 | 仓库 | 用途 |
|---|---|---|
| origin | `github.com/blacksamuraiiii/hermes-gadget-sdk` | 我的 fork，日常提交与推送目标 |
| upstream | `github.com/Adolanium/hermes-gadget-sdk` | 上游主仓库，只读，定期拉取 |

同步上游（**不直接 merge main**，按待办 2.1 分流 cherry-pick）：

```bash
git fetch upstream --tags
git rev-list --count 469978c..upstream/main   # fork 点以来的提交数
git log --oneline upstream/main | head
```

## 关键速记（动手前必看）

- **本板不在上游支持表**，port 已由本 fork 完成（v0.1）；同为 240 圆屏的 Spotpear 1.28-BOX 驱动与引脚都不同，不可照抄 profile。
- **采样率 24 kHz 是板级常量**，照抄 16 kHz 会变调变速。
- **屏是 SPI 40 MHz，不是 I2C**；排线长会花屏，改线先降 20 MHz 验证。
- **USB 链路下没有 Wi-Fi 配网**：要回 Wi-Fi 需注释 `CONFIG_HG_LINK_USB_SERIAL` 重编，代码不用改。
- **刷写与 console 只能开 COM5**：relay 独占该口，先停 relay；`tools/console_cmd.py` 会自动让路再恢复。
- **字体是全局改动，不是板级开关**：core 的 `font.hpp / canvas.cpp / ui.cpp` 被改成 6×12、行高 14，所有板跟着变；上游同期走 Latin-1 路线，合并时是最大冲突点（待办 1B）。
- **插件有两份副本**：gateway 加载 `~/hermes-sync/plugins/gadget/`，改项目 `plugin/` 后必须同步并 `diff -rq` 确认。
- **`hermes gadget update --latest` 指向上游 release**（`plugin/releases.py` 写死仓库地址），认不到 fork 自己的包。

## 笔记索引

| 文件 | 内容 | 状态 |
|---|---|---|
| `硬件-墨迹板参数.md` | 引脚表、屏与音频硬参、实机实测参数、恢复出厂固件 | 现行 |
| `路线-hermes-gadget.md` | 三件套、port 工作量评估、本板 `make()` 目标值、硬差异 | 现行（评估已落地） |
| `决策-台账.md` | 为什么这样定；悬而未决逐条附现状 | 现行 |
| `验收-0.1实测记录.md` | 0.1 全部实测证据与结论，0.2 回归基线 | 现行 |
| `环境-本机工具链与坑.md` | 本机 IDF / 烧录 / relay 事实、操作坑、交接命令 | 现行 |
| `待办-20261026-v0.2.md` | 0.2 出货定义、三项任务、上游分流表、待拍板五项 | 现行 |
| `设计-语音唤醒.md` | MultiNet 选型、model 分区、24k→16k 重采样、常开麦接缝、资源账与门槛 | 现行（设计稿，未实现） |
| `归档/` | 已被取代的计划与审查、弃用路线 | 历史，结论勿直接引用 |

## 命名与收录约定

- 文件名 `类别-主题.md`。类别：硬件 / 路线 / 决策 / 验收 / 环境 / 待办 / 设计 / 上游。待办带日期与版本号。
- **一类信息只放一处**：决策进台账，实测进验收，坑与环境事实进环境，一次性计划与审查完成后进 `归档/`。
- 每份文件开头三行内写清「用途 / 核对日期 / 取代关系」。
- 一律中文；命令、路径、日志原文不改写，`[码]/[测]/[推]/[?]` 证据标记沿用。
- 上游 `docs/` 里的文档引用不要改；本项目自己的引用随文件改名同步。

## 待办

见 `待办-20261026-v0.2.md`。当前三项：修复 0.1 遗留、跟踪上游（220 提交待分流）、语音唤醒设计。
