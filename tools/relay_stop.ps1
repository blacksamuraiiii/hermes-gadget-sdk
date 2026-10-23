# Stop the USB relay (COM5 holder). Safe to call when nothing is running.
$ErrorActionPreference = 'Stop'

$running = Get-CimInstance Win32_Process -Filter "Name='python.exe' OR Name='pythonw.exe'" |
  Where-Object { $_.CommandLine -like '*usb_relay*' }
if (-not $running) {
  'relay is not running'
  exit 0
}
$pids = ($running | ForEach-Object ProcessId) -join ','
$running | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
"relay stopped (pid $pids)"
