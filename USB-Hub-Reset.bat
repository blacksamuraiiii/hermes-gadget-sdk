@echo off
setlocal
chcp 936 >nul
title ESP32-S3 USB 链路复位
set "PS1=%~dp0tools\usb_hub_reset.ps1"

if /i "%~1"=="-DryRun" goto DRYRUN

:ELEVATE
net session >nul 2>&1
if not errorlevel 1 goto RUN
echo.
echo [需要管理员权限] 正在请求提权, 请在 UAC 弹窗点 [是] ...
powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs" >nul 2>&1
if errorlevel 1 (
  echo 提权失败: 请右键本文件 - 以管理员身份运行。
  pause
)
goto END

:RUN
echo ==========================================================
echo   ESP32-S3 USB 链路复位
echo   适用: 设备管理器代码 43 / 请求 USB 设备描述符失败 / 端口重置失败
echo   做法: 停 relay -^> 清失败占位节点 -^> 重启对应 hub 面 -^> 回读, 没回来再重试
echo   注意: 该 hub 上的设备会瞬断数秒 (键鼠可能在上面)
echo ==========================================================
echo.
echo [提示] 板子此刻在线时本脚本不做任何改动。
choice /c YN /n /m "继续? [Y=继续 / N=退出] "
if errorlevel 2 goto END
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %2 %3
echo.
echo 判据: 末行 VERDICT=RESTORED 才算恢复; REBOOT REQUIRED 就只有重启能解决。
echo 详细日志: %TEMP%\usbhub_reset.log
pause
goto END

:DRYRUN
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -DryRun
pause
goto END

:END
endlocal
