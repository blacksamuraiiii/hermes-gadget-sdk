# 固件 v0.1（墨迹板提醒屏）

本目录是本项目第一阶段的固件存档，2026-10-23 构建，源码对应提交即本目录所在提交。

## 构成

| 文件 | 烧写地址 | 大小（字节） |
|---|---|---|
| `bootloader.bin` | `0x0` | 20,768 |
| `partition-table.bin` | `0x8000` | 3,072 |
| `ota_data_initial.bin` | `0xf000` | 8,192 |
| `hermes_gadget.bin` | `0x20000` | 1,405,952 |

校验值见 `SHA256SUMS`。

## 烧写（Windows 侧，esptool v5 语法）

烧前先停 relay（COM5 独占），把本目录四个 bin 复制到 Windows 路径后：

```powershell
python.exe -m esptool --chip esp32s3 --port COM5 --baud 921600 write-flash `
  --flash-mode dio --flash-freq 80m --flash-size 16MB `
  0x0 bootloader.bin 0x8000 partition-table.bin 0xf000 ota_data_initial.bin 0x20000 hermes_gadget.bin
```

成败判据：出现 `Hash of data verified.` 与 `Hard resetting via RTS pin...` 两条。

## 这一版固件的特征

- 传输：USB Serial/JTAG（`CONFIG_HG_LINK_USB_SERIAL=y`），默认服务器 `ws://usb/gadget`，需配 relay。
- 显示：中文字库（GB2312 一级 3755 字）+ 6×12 ASCII，26 列 × 5 行，行高 14，`charset=utf-8`。
- 音频：24 kHz 输入/输出，cue 播报；语音回合入口由插件侧 `voice=false` 关闭（固件不区分）。
- 无 Wi-Fi 配网依赖；`screen_timeout=0` 时屏不休眠（USB 供电场景）。

## 重新构建

```bash
cd firmware/esp32
export PATH=/usr/bin:$PATH; unset VIRTUAL_ENV CONDA_PREFIX
. ~/esp/esp-idf/export.sh            # 本机 IDF 的 venv 绑 python3.11
idf.py build                          # 产物在 build/
```

字体表由 `tools/make_cjk_font.py` 从 BDF 生成，换字号要重新生成并重编。
