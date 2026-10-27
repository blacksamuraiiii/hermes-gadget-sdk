# Flash just the application image over USB and bring the relay back.
#
# The relay holds COM5, and the plugin watchdog puts it back within seconds, so the stop and the
# write have to happen in one go. App-only: the partition table and srmodels.bin stay untouched
# (a partition-table change needs a full-chip write, see notes/设计-语音唤醒.md).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\flash_app.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\flash_app.ps1 -Port COM7
param([string]$Port = 'COM5')

$ErrorActionPreference = 'Continue'
$proj = Split-Path -Parent $PSScriptRoot
$build = Join-Path $proj 'firmware\esp32\build'
$bin = Join-Path $build 'hermes_gadget.bin'
$py = 'C:\Users\black_samurai\miniconda3\envs\gadget-relay\python.exe'
$ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$log = Join-Path $env:TEMP 'hg_flash_app.log'

if (-not (Test-Path $bin)) { Write-Host "no app image at $bin - build first"; exit 1 }

& $ps -NoProfile -ExecutionPolicy Bypass -File (Join-Path $proj 'tools\relay_stop.ps1') | Out-Null
Get-CimInstance Win32_Process -Filter "Name='python.exe' OR Name='pythonw.exe'" |
  Where-Object { $_.CommandLine -like '*usb_relay*' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 900

$ok = $false
for ($i = 1; $i -le 3 -and -not $ok; $i++) {
  "== attempt $i ==" | Out-File -FilePath $log -Encoding utf8
  & $py -m esptool --chip esp32s3 -p $Port -b 460800 --before default-reset --after hard-reset `
    write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x20000 $bin *>&1 |
    Out-File -FilePath $log -Append -Encoding utf8
  $ok = (Select-String -Path $log -Pattern 'Hash of data verified' -Quiet)
}
if ($ok) { Write-Host 'FLASH OK' } else { Write-Host 'FLASH FAILED - see %TEMP%\hg_flash_app.log' }

Start-Sleep -Milliseconds 800
& $ps -NoProfile -ExecutionPolicy Bypass -File (Join-Path $proj 'tools\relay_start.ps1') | Out-Null
