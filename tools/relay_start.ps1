# Start the USB relay that carries a gadget's link on COM5 - no console window.
# Idempotent: prints "relay already running (pid N)" when one is up, so the plugin
# can start it on every gateway start without stacking copies.
$ErrorActionPreference = 'Stop'
# Everything is derived from this script's own location plus the relay interpreter:
#   RELAY_PYTHONW  full path to pythonw.exe of the env that has pyserial+websockets
#   (fallback)     a conda env named gadget-relay under the user profile
$workdir = $PSScriptRoot
$pythonw = $env:RELAY_PYTHONW
if (-not $pythonw) {
  $candidates = @(
    (Join-Path $env:USERPROFILE 'miniconda3\envs\gadget-relay\pythonw.exe'),
    (Join-Path $env:USERPROFILE 'anaconda3\envs\gadget-relay\pythonw.exe'),
    (Join-Path $workdir '..\..\.venv\Scripts\pythonw.exe')
  )
  $pythonw = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
}
if (-not $pythonw) {
  'relay: set RELAY_PYTHONW to a pythonw.exe that has pyserial and websockets (e.g. a conda env named gadget-relay)'
  exit 1
}

function Find-Relay {
  Get-CimInstance Win32_Process -Filter "Name='python.exe' OR Name='pythonw.exe'" |
    Where-Object { $_.CommandLine -like '*usb_relay*' }
}

$running = Find-Relay
if ($running) {
  "relay already running (pid $(($running | ForEach-Object ProcessId) -join ','))"
  exit 0
}

Start-Process -FilePath $pythonw -WorkingDirectory $workdir -WindowStyle Hidden -ArgumentList @(
  '-u', 'usb_relay.py', '--target', 'ws://127.0.0.1:8765', '--path', '/gadget',
  '--log-level', 'INFO', '--log-file', 'relay.log'
)
Start-Sleep -Seconds 3

$running = Find-Relay
if ($running) {
  "relay started (pid $(($running | ForEach-Object ProcessId) -join ','))"
  exit 0
}
'relay did not start'
exit 1
