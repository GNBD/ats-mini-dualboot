# SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
# SPDX-License-Identifier: MIT
#requires -Version 5.1
<#
  flash-lite.ps1 - ATS Mini Boot Manager (lite) 플래셔

  허용 offset 은 아래뿐입니다 (그 외 전부 거부):
    -Target 16MB -Slot recovery : 0x860000 (16MB 기기의 recovery 슬롯, 테스트용)
    -Target 8MB  -Slot recovery : 0x660000 (N8R2 lite 파티션의 recovery 슬롯)
    -Target 8MB  -Slot app0     : 0x10000  (N8R2 메인 앱 슬롯)
    -Target 8MB  -Slot app1     : 0x290000 (N8R2 메인 앱 슬롯 2)
    -Install                    : N8R2 전체 설치 (0x0 / 0x8000 / 0xE000 / 0x10000 / 0x660000)

  16MB 파티션 테이블 / 앱1(app1) 은 절대 플래시하지 않습니다.
  8MB 파티션 테이블을 16MB 기기에 쓰면 OTA/LittleFS 오프셋이 어긋나므로
  8MB 보드(N8R2)에만 사용하세요.

  N8R2 전체 설치 예)
    .\flash-lite.ps1 -Install -Target 8MB -Port COM7

  예)
    .\flash-lite.ps1 -Target 8MB -Port COM7
    .\flash-lite.ps1 -Target 8MB -Slot app0
    .\flash-lite.ps1 -Target 8MB -Slot app1
    .\flash-lite.ps1 -Image .\build\esp32.esp32.esp32s3\ats-mini-recovery-lite.ino.bin
#>
[CmdletBinding()]
param(
  [string]$Port,
  [string]$Image,
  [ValidateSet('16MB','8MB')]
  [string]$Target = '16MB',
  [ValidateSet('recovery','app0','app1')]
  [string]$Slot = 'recovery',
  [switch]$Install,
  [int]$Baud = 460800,
  [switch]$Touch,
  [switch]$NoTouch,
  [string]$Esptool,
  [int]$WaitSeconds = 60
)

$ErrorActionPreference = 'Stop'

$Allow = @{
  '16MB' = @{ 'recovery' = '0x860000' }
  '8MB'  = @{ 'recovery' = '0x660000'; 'app0' = '0x10000'; 'app1' = '0x290000' }
}

$InstallPlan = @(
  @{ Off = '0x000000'; Name = 'ats-mini-recovery-lite-1.0.0-bootloader.bin' },
  @{ Off = '0x008000'; Name = 'ats-mini-recovery-lite-1.0.0-partitions-8MB.bin' },
  @{ Off = '0x00E000'; Name = 'boot_app0.bin' },
  @{ Off = '0x010000'; Name = 'ats-mini-app0-1.0.0-n8r2.bin' },
  @{ Off = '0x0660000'; Name = 'ats-mini-recovery-lite-1.0.0.bin' }
)

$Plan = @()
if ($Install) {
  if ($Target -ne '8MB') {
    throw "거부됨: -Install 은 N8R2(8MB) 전용입니다. -Target 8MB 를 지정하세요."
  }
  foreach ($e in $InstallPlan) {
    $p = Join-Path $PSScriptRoot $e.Name
    if (-not (Test-Path -LiteralPath $p)) { throw "설치에 필요한 파일이 없습니다: $($e.Name)" }
    $Plan += [pscustomobject]@{ Off = $e.Off; Img = $p }
  }
} else {
  $Offset = $Allow[$Target][$Slot]
  if (-not $Offset) {
    throw "거부됨: -Target $Target / -Slot $Slot 은 허용되지 않습니다 (16MB:recovery, 8MB:recovery|app0|app1 만 가능)."
  }
}

function Find-Esptool {
  param([string]$Hint)
  if ($Hint -and (Test-Path -LiteralPath $Hint)) { return $Hint }
  $c = Get-Command esptool -ErrorAction SilentlyContinue
  if ($c) { return $c.Source }
  $roots = @("$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py",
             "$env:USERPROFILE\AppData\Local\Arduino15\packages\esp32\tools\esptool_py")
  foreach ($r in $roots) {
    if (Test-Path -LiteralPath $r) {
      $hit = Get-ChildItem -LiteralPath $r -Recurse -Filter esptool.exe -ErrorAction SilentlyContinue |
             Sort-Object FullName -Descending | Select-Object -First 1
      if ($hit) { return $hit.FullName }
    }
  }
  throw "esptool.exe 를 찾을 수 없습니다. -Esptool 로 경로를 지정하세요."
}

function Get-AtsDevices {
  Get-CimInstance Win32_PnPEntity | Where-Object { $_.DeviceID -match 'VID_303A' } | ForEach-Object {
    $desc = ''
    try { $desc = (Get-PnpDeviceProperty -InstanceId $_.DeviceID -KeyName DEVPKEY_Device_BusReportedDeviceDesc -ErrorAction Stop).Data } catch {}
    [pscustomobject]@{
      Name     = $_.Name
      DeviceID = $_.DeviceID
      Desc     = $desc
      Com      = ([regex]::Match($_.Name, 'COM\d+')).Value
    }
  }
}

function Wait-RomPort {
  param([int]$Seconds)
  for ($i = 0; $i -lt ($Seconds * 2); $i++) {
    Start-Sleep -Milliseconds 500
    $rom = Get-AtsDevices | Where-Object { $_.Com -and $_.Desc -ne 'TinyUSB CDC' } | Select-Object -First 1
    if ($rom) { return $rom }
  }
  return $null
}

function Invoke-TouchAndWait {
  param($TouchPort, [int]$Seconds)
  "touch   : $($TouchPort.Com) (TinyUSB CDC) @1200"
  try {
    $sp = New-Object System.IO.Ports.SerialPort
    $sp.PortName = $TouchPort.Com
    $sp.BaudRate = 1200
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    $sp.Open()
    Start-Sleep -Milliseconds 150
    $sp.Close()
  } catch {
    Write-Warning "1200-touch open: $($_.Exception.Message) (무시하고 계속)"
  }
  "waiting : ROM 다운로드 모드 포트 (최대 ${Seconds}s)..."
  Wait-RomPort -Seconds $Seconds
}

$esp = Find-Esptool -Hint $Esptool

if (-not $Install) {
  if (-not $Image) {
    # 슬롯별 기본 이미지: recovery -> Lite, app0/app1 -> 메인 앱
    $default = if ($Slot -eq 'recovery') {
      'ats-mini-recovery-lite-1.0.0.bin'
    } else {
      'ats-mini-app0-1.0.0-n8r2.bin'
    }
    $exact = Join-Path $PSScriptRoot $default
    if (Test-Path -LiteralPath $exact) {
      $Image = $exact
    } elseif ($Slot -eq 'recovery') {
      $rel = Get-ChildItem -LiteralPath $PSScriptRoot -Filter 'ats-mini-recovery-lite-*.bin' -ErrorAction SilentlyContinue |
             Where-Object { $_.Name -notmatch 'bootloader|partitions' } |
             Sort-Object Name | Select-Object -First 1
      if ($rel) { $Image = $rel.FullName }
      else { $Image = Join-Path $PSScriptRoot 'build\esp32.esp32.esp32s3\ats-mini-recovery-lite.ino.bin' }
    } else {
      throw "메인 앱 이미지가 없습니다: $default (보내서 파일명을 맞추거나 -Image 로 지정하세요)."
    }
  }
  if (-not (Test-Path -LiteralPath $Image)) { throw "이미지 파일이 없습니다: $Image" }
  $Plan += [pscustomobject]@{ Off = $Offset; Img = $Image }
}

"esptool : $esp"
if ($Install) {
  "mode    : Install (N8R2 전체 설치, $($Plan.Count) 영역)"
} else {
  "mode    : 단일 플래시 (target=$Target slot=$Slot)"
}
foreach ($p in $Plan) {
  $n = Split-Path -Leaf $p.Img
  $len = (Get-Item -LiteralPath $p.Img).Length
  $md5 = (Get-FileHash -LiteralPath $p.Img -Algorithm MD5).Hash
  "plan    : $($p.Off)  $n  ($len bytes, md5=$md5)"
}

$dev = $null

if ($Port) {
  $dev = Get-AtsDevices | Where-Object { $_.Com -eq $Port } | Select-Object -First 1
  if (-not $dev) {
    $dev = [pscustomobject]@{ Name = $Port; Desc = '(지정)'; Com = $Port }
  }
}

if (-not $dev) {
  $tusb = Get-AtsDevices | Where-Object { $_.Desc -eq 'TinyUSB CDC' -and $_.Com } | Select-Object -First 1
  $hwcdc = Get-AtsDevices | Where-Object { $_.Com -and $_.Desc -ne 'TinyUSB CDC' } | Select-Object -First 1

  if ($Touch -or ($tusb -and -not $NoTouch -and -not $hwcdc)) {
    if (-not $tusb) { throw "TinyUSB CDC 포트를 찾을 수 없습니다." }
    $dev = Invoke-TouchAndWait -TouchPort $tusb -Seconds $WaitSeconds
    if (-not $dev) { throw "ROM 다운로드 모드로 전환되지 않았습니다." }
  } elseif ($hwcdc) {
    $dev = $hwcdc
  } elseif ($tusb) {
    $dev = Invoke-TouchAndWait -TouchPort $tusb -Seconds $WaitSeconds
    if (-not $dev) { throw "ROM 다운로드 모드로 전환되지 않았습니다." }
  } else {
    throw "ESP32-S3(VID_303A) COM 포트를 찾을 수 없습니다. 보드를 연결하거나 -Port 를 지정하세요."
  }
}

"flash   : $($dev.Com) ($($dev.Desc))"
$argList = @('--chip', 'esp32s3', '--port', $dev.Com, '--baud', "$Baud",
             '--before', 'default-reset', '--after', 'hard-reset', 'write-flash')
foreach ($p in $Plan) { $argList += @($p.Off, $p.Img) }
& $esp @argList
$code = $LASTEXITCODE
"exit    : $code"
exit $code
