# moji-0.3 固件（Movecall moji ESP32-S3，16 MB flash）· 正式版

0.3 在 0.2 的语音唤醒之上修了 6 处固件问题，并把显示层做了一轮收口（16 px 统一字表、
四行正文、机器人脸与连接灯）。**没有动分区表**：从 0.2 升上来可以只刷 app
（`app-flash` / OTA），不必整片刷。

这是 GitHub 的**正式发布**（`releases/latest` 指向本 tag）：`hermes gadget update <device> --latest`
可以直接升到这一版；离线刷写按下面的命令。

## 这一版改了什么

**固件**

| 改动 | 提交 |
|---|---|
| 唤醒引擎跟着麦克风走：播报结束后不必等回合落定就能再唤醒 | `9f1a175` |
| 唤醒阈值恢复 `CONFIG_HG_WAKE_THRESHOLD / 100`（0.20），启动日志打印实际值 | `af17f3c` |
| bring-up 诊断日志收进 `CONFIG_HG_WAKE_DIAG`，默认关闭 | `d23150c` |
| tap 持麦期间不再投 Mic 事件（空闲期不再刷 `event queue full`） | `efca272` |
| 网关重启期间不再弹 `connect timeout`：连续 3 次未握手才报错 | `7a2f493` |
| 唤醒引擎启动失败时释放已建的模型/AFE/队列，不再把 PSRAM 和 SPIFFS 句柄留到下次重启 | `bb67a32` |

**显示**

| 改动 | 提交 |
|---|---|
| 16 px 思源/Noto Sans SC 一套通吃中英，拉丁 4× 超采样修 `T`/`E`/`F`/`j` 缺笔 | `619fa95` |
| 正文 2 行 → 4 行；无触摸板改 36 px 弧线让位带，页头与分隔线不再被圆弧切 | `619fa95` |
| 机器人脸画满整块玻璃，天线球改作连接状态灯（绿/黄/红） | `619fa95` `2bc47e3` |
| 回复页页头 `Running` / `Speaking` / `Ready` | `78e15e5` |
| 口播把 `%` 读作「个百分点」 | `7e1f5ad` |
| 修「脸 → 文字页」切换的页头像素残留 | `b0aec71` |

## 刷写（Windows 侧，USB 直连）

先停 relay（它占着 COM5；插件看门狗默认已关，但仍建议停和刷连着做）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\relay_stop.ps1
```

**只刷 app**（0.2 → 0.3 推荐，分区表没变）：

```powershell
python -m esptool --chip esp32s3 -p COM5 -b 460800 --before default-reset --after hard-reset `
  write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB `
  0x20000 hermes_gadget.bin
```

**整片刷**（换板子、或想连模型分区一起重写时）：

```powershell
python -m esptool --chip esp32s3 -p COM5 -b 460800 --before default-reset --after hard-reset `
  write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB `
  0x0      bootloader.bin `
  0x8000   partition-table.bin `
  0xf000   ota_data_initial.bin `
  0x800000 srmodels.bin `
  0x20000  hermes_gadget.bin
```

或者直接刷合并好的镜像（0x0 起，不含模型分区，同样会清掉设备设置、需要重新配对）：

```powershell
python -m esptool --chip esp32s3 -p COM5 -b 460800 --before default-reset --after hard-reset `
  write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB `
  0x0 hermes-gadget-movecall-moji-esp32s3-0.3.0.bin
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\relay_start.ps1
```

`--flash-size 16MB` 必带：这台机器上 esptool 5.6.0 不带它会把 `0x800000` 判成越界。

## 校验

```bash
sha256sum -c SHA256SUMS
```

设备起来后 `tools/relay.log` 应出现（注意阈值是 **0.20**；0.2 那份 README 写的 `threshold 20`
是日志失真，0.3 已改成打印实际值）：

```
hg.wake: wake word ready: mn5q8_cn, threshold 0.20, frame 512 samples at 16000 Hz
hg.wake: resume: microphone started
hg: online (paired)
```

播报结束后再说「你好小墨」应能立刻命中（0.2 里有 30 秒左右的死窗，`9f1a175` 修掉了）：

```
hg.codec: playback ended: ... 0 underruns ...
hg.wake: hit: command 0 at NN percent
```

## 文件

| 文件 | 大小 | 说明 |
|---|---|---|
| `hermes_gadget.bin` | 2,368,240 B | app（槽 `0x3e0000`，余 42%） |
| `hermes-gadget-movecall-moji-esp32s3-0.3.0.bin` | 2,499,312 B | 整片合并镜像（bootloader + 分区表 + ota_data + app），从 `0x0` 写 |
| `hermes-gadget-movecall-moji-esp32s3-0.3.0-app.bin` | 2,368,240 B | 只刷 app 用（与 `hermes_gadget.bin` 同内容） |
| `srmodels.bin` | 2,203,819 B | esp-sr 模型（mn5q8_cn），只有整片刷才需要 |
| `partition-table.bin` | 3,072 B | 与 0.2 相同（`ota_0` 0x20000/0x3e0000、`model` 0x800000） |
| `ota_data_initial.bin` | 8,192 B | 指向 `ota_0` |
| `bootloader.bin` | 20,768 B | ESP-IDF 原样 |
| `manifest.json` | — | 给 `hermes gadget update` 用，带 `tag: "moji-0.3"` 字段 |

## 已知边界

- **唤醒阈值还没标定**：当前固定 0.20（20%），标定要 15/20/25 三档 × 0.5/1/2 m × 安静/噪声各 20 遍，
  见 `notes/待办-20261030-moji-0.3.md` 任务 A2。
- 无取消键、无触摸：`caps.dashboard` 不上报，提醒走卡片 + cue。
- OTA：`--latest` 已指向本版，0.2 设备可直接升上来（分区表未变）；OTA 链路的真机实测（0.2 → 0.3）仍待做。
