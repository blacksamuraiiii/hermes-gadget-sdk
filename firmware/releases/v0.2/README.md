# moji-0.2 固件（Movecall moji ESP32-S3，16 MB flash）

0.2 相对 0.1 的两件事：**语音唤醒打通**（关键词「你好小墨」/「小墨小墨」→ 本地播「我在」→
开一轮免提对话），以及随之而来的**分区表变更**（app 涨到 2.1 MB，模型分区上移）。

## 这一版必须整片刷

分区表变了（ota 槽 0x3e0000 ×2、model 上移到 0x800000），从 0.1 升上来**不能只刷 app**，
否则 model 分区还在老地址、唤醒引擎加载不到模型。整片刷一次之后，后续再用 `app-flash` / OTA。

## 刷写（Windows 侧，USB 直连）

先停 relay（它占着 COM5；插件看门狗 1~5 秒会把 relay 拉起来，所以停和刷要连着做）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\relay_stop.ps1
python -m esptool --chip esp32s3 -p COM5 -b 460800 --before default-reset --after hard-reset `
  write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB `
  0x0      bootloader.bin `
  0x8000   partition-table.bin `
  0xf000   ota_data_initial.bin `
  0x800000 srmodels.bin `
  0x20000  hermes_gadget.bin
powershell -NoProfile -ExecutionPolicy Bypass -File tools\relay_start.ps1
```

`--flash-size 16MB` 必带：这台机器上 esptool 5.6.0 不带它会把 `0x800000` 判成越界。

## 校验

```bash
sha256sum -c SHA256SUMS        # 五个文件
```

设备起来后 `tools/relay.log` 应该依次出现：

```
MODEL_LOADER: The partition size is 4096 KB
hg.wake: wake word: 512 samples per MultiNet frame
hg.wake: wake word ready: mn5q8_cn, threshold 20, frame 512 samples at 16000 Hz
hg.wake: resume: microphone started / resume: tap on
hg: online (paired)
```

喊「你好小墨」后应看到 `hg.wake: hit: command 0 at NN percent` → `hg.codec: playback ended ...
58752 B played`（「我在」，1.23 秒）→ 屏幕进 listening。

## 文件

| 文件 | 大小 | 说明 |
|---|---|---|
| `hermes_gadget.bin` | 2,199,840 B | app（槽 0x3e0000，余 46%） |
| `srmodels.bin` | 2,203,819 B | esp-sr 模型（mn5q8_cn），整片刷才需要 |
| `partition-table.bin` | 3,072 B | 0.2 新布局 |
| `ota_data_initial.bin` | 8,192 B | 指向 ota_0 |
| `bootloader.bin` | 20,768 B | ESP-IDF 原样 |

模型与 app 的字节数都记在这里，是为了下次核对"屏幕上报的版本/构建时间是否就是这次刷的镜像"。
