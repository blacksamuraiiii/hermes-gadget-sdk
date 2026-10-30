# ESP32-S3 USB 链路复位（relay 感知 / 可重试 / 以回读为准）
#
#   .\usb_hub_reset.ps1 -DryRun   # 只诊断，不改动任何东西（不需要管理员）
#   .\usb_hub_reset.ps1           # 需要管理员；板子已在线时什么都不做
#   .\usb_hub_reset.ps1 -Force    # 板子在线也强行复位
#
# 判据只看最后一行 VERDICT：
#   RESTORED        板子回到总线（COM 口同时回读一遍）
#   ALREADY ONLINE  板子本来就在，什么都没动
#   REBOOT REQUIRED 端口被系统扣住（hub 面进入等待重启），只有重启能解决
#   STILL ABSENT    复位做了也没回来：换口 / 换线 / 按住 BOOT 插线
#
# 为什么不能只做一次 "restart-device + scan-devices"：
#   hub 面一重启，其上的设备会被重新枚举；全速设备（ESP32-S3 的 USB 是 FS）
#   偶尔会在这一瞬间失败，Windows 就地留下 VID_0000&PID_0001/0002 失败占位节点，
#   而 /scan-devices 不会重试一个"端口重置失败"的端口 —— 于是板子一直不在总线上，
#   只有重启电脑才清掉。所以这里要：先停 relay（它握着 COM5）→ 清占位节点 →
#   重启 hub 面 → 回读 → 没回来再来一轮。

[CmdletBinding()]
param(
    [switch]$DryRun,
    [switch]$Force,
    [string]$BoardPattern = 'USB\VID_303A*',
    [int]$MaxAttempts = 3
)

$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::GetEncoding(936) } catch { }

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$LogPath = Join-Path $env:TEMP 'usbhub_reset.log'

function Say([string]$msg) {
    $msg
    Add-Content -Path $LogPath -Value ('[{0}] {1}' -f (Get-Date -Format 'HH:mm:ss'), $msg) -ErrorAction SilentlyContinue
}

function Get-BoardNodes([switch]$PresentOnly) {
    $q = if ($PresentOnly) { Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue } else { Get-PnpDevice -ErrorAction SilentlyContinue }
    @($q | Where-Object { $_.InstanceId -like $BoardPattern })
}

function Get-FailedNodes {
    @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -like 'USB\VID_0000&PID_*' })
}

function Get-ParentOf([string]$instanceId) {
    $v = (Get-PnpDeviceProperty -InstanceId $instanceId -KeyName 'DEVPKEY_Device_Parent' -ErrorAction SilentlyContinue).Data
    if ($v) { [string]$v } else { '' }
}

function Invoke-Pnputil([string[]]$pnputilArgs) {
    $out = (& pnputil.exe @pnputilArgs 2>&1 | Out-String).Trim()
    [pscustomobject]@{ Output = $out; Code = $LASTEXITCODE }
}

Say '=========================================================='
Say ('ESP32-S3 USB 链路复位  ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Say ('日志: ' + $LogPath)
Say '=========================================================='

# ---- 1. 现状 ----
$boardAll = @(Get-BoardNodes)
$boardUp = @(Get-BoardNodes -PresentOnly | Where-Object { $_.Status -eq 'OK' })
Say ('[1/6] 板子节点: 历史 {0} 个, 此刻在线 {1} 个' -f $boardAll.Count, $boardUp.Count)
foreach ($n in $boardUp) { Say ('   在线: ' + $n.FriendlyName + ' | ' + $n.InstanceId) }

if ($boardUp.Count -gt 0 -and -not $Force) {
    Say 'VERDICT: ALREADY ONLINE（板子此刻在总线上，脚本不做任何改动；要强行复位加 -Force）'
    exit 0
}

$failed = Get-FailedNodes
Say ('[2/6] 失败占位节点: {0} 个' -f $failed.Count)
foreach ($f in $failed) {
    Say ('   ' + $f.Status + ' | ' + $f.InstanceId + ' | parent=' + (Get-ParentOf $f.InstanceId))
}

# ---- 2. 目标 hub 面：失败节点的父设备 / 板子复合节点的父设备；
#         板子自己的子接口（*&MI_*）的父设备是板子本身，必须排除，否则会去"重启设备自己" ----
$targets = @()
foreach ($f in $failed) {
    $p = Get-ParentOf $f.InstanceId
    if ($p -like 'USB\VID_*' -and $p -notlike $BoardPattern) { $targets += $p }
}
foreach ($n in $boardAll) {
    if ($n.InstanceId -like '*&MI_*') { continue }
    $p = Get-ParentOf $n.InstanceId
    if ($p -like 'USB\VID_*' -and $p -notlike $BoardPattern) { $targets += $p }
}
$targets = @($targets | Sort-Object -Unique | Where-Object { Get-PnpDevice -InstanceId $_ -ErrorAction SilentlyContinue })
if ($targets.Count -eq 0) {
    $targets = @(@('USB\VID_2109&PID_2817\MSFT20', 'USB\VID_2109&PID_0817\MSFT30') |
        Where-Object { Get-PnpDevice -InstanceId $_ -ErrorAction SilentlyContinue })
}
if ($targets.Count -gt 0) {
    Say ('[3/6] 目标 hub 面: ' + ($targets -join ', '))
} else {
    Say '[3/6] 目标 hub 面: 没找到（板子与失败节点都没有父设备记录）'
    Say 'VERDICT: STILL ABSENT（没有可复位的 hub；把板子改插主板直连口后再试）'
    exit 1
}

$relayUp = @(Get-CimInstance Win32_Process -Filter "Name='python.exe' OR Name='pythonw.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*usb_relay*' })
Say ('[4/6] relay: ' + $(if ($relayUp.Count -gt 0) { 'running (pid ' + (($relayUp | ForEach-Object ProcessId) -join ',') + ')' } else { 'not running' }))

if ($DryRun) {
    Say '[5/6] DryRun：到此为止，未改动任何设备。恢复时去掉 -DryRun 重跑（需要管理员）。'
    Say 'VERDICT: DRY RUN'
    exit 0
}

# ---- 3. 停 relay（它独占 COM5，重启 hub 时握着句柄会把端口留成失败态）----
$relayStop = Join-Path $here 'relay_stop.ps1'
$relayStart = Join-Path $here 'relay_start.ps1'
if ($relayUp.Count -gt 0 -and (Test-Path $relayStop)) {
    (& $relayStop 2>&1 | Out-String).Trim() -split "`r?`n" | Where-Object { $_ } | ForEach-Object { Say ('   ' + $_) }
    Start-Sleep -Seconds 1
}

# ---- 4. 清占位节点 + 重启 hub 面 + 回读，最多 MaxAttempts 轮 ----
$restored = $false
$rebootRequired = $false
for ($i = 1; $i -le $MaxAttempts -and -not $restored; $i++) {
    Say ('[5/6] --- 第 ' + $i + ' 轮 ---')
    foreach ($f in (Get-FailedNodes)) {
        $r = Invoke-Pnputil @('/remove-device', $f.InstanceId)
        Say ('   remove-device ' + $f.InstanceId + ' -> rc=' + $r.Code + ' ' + $r.Output)
    }
    foreach ($t in $targets) {
        $r = Invoke-Pnputil @('/restart-device', $t)
        if ($r.Output -match '(?i)reboot|重新启动|3010') {
            $rebootRequired = $true
            Say ('   restart-device ' + $t + ' -> 进入等待重启状态: ' + $r.Output)
        } else {
            Say ('   restart-device ' + $t + ' -> rc=' + $r.Code + ' ' + $r.Output)
        }
    }
    Start-Sleep -Seconds 4
    $null = Invoke-Pnputil @('/scan-devices')
    Start-Sleep -Seconds 3
    $up = @(Get-BoardNodes -PresentOnly | Where-Object { $_.Status -eq 'OK' })
    $nowFailed = Get-FailedNodes
    Say ('   复读: 板子在线 ' + $up.Count + ' 个, 失败节点 ' + $nowFailed.Count + ' 个')
    if ($up.Count -gt 0) { $restored = $true }
}

# ---- 5. relay 拉回来（看门狗 120 s 也会拉，别等）----
if ($relayUp.Count -gt 0 -and (Test-Path $relayStart)) {
    (& $relayStart 2>&1 | Out-String).Trim() -split "`r?`n" | Where-Object { $_ } | ForEach-Object { Say ('   ' + $_) }
}

# ---- 6. 最终回读与判据 ----
Say '[6/6] 最终回读'
$final = @(Get-BoardNodes -PresentOnly)
if ($final.Count -gt 0) {
    foreach ($n in $final) { Say ('   ' + $n.Status + ' | ' + $n.Class + ' | ' + $n.FriendlyName + ' | ' + $n.InstanceId) }
} else {
    Say '   板子: NOT FOUND'
}
foreach ($p in @(Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue)) {
    if ($p.InstanceId -like 'USB\VID_303A*') { Say ('   COM: ' + $p.Status + ' | ' + $p.FriendlyName + ' | ' + $p.InstanceId) }
}
$left = Get-FailedNodes
Say ('   残留失败节点: ' + $left.Count)
foreach ($f in $left) { Say ('   ' + $f.Status + ' | ' + $f.Problem + ' | ' + $f.InstanceId) }

if ($final.Count -gt 0) {
    Say 'VERDICT: RESTORED'
    exit 0
}
if ($rebootRequired) {
    Say 'VERDICT: REBOOT REQUIRED（该 hub 面在等待重启，pnputil 已不再受理；重启后才能清端口状态）'
    exit 2
}
Say 'VERDICT: STILL ABSENT（复位无效：把板子改插主板直连口 / 换数据线 / 拔 hub 上游电源 3 秒再插）'
exit 1
