# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

param(
  [Parameter(Mandatory = $true)][string]$Runtime,
  [Parameter(Mandatory = $true)][string]$Project,
  [Parameter(Mandatory = $true)][string]$TestRoot
)

$signal = Join-Path $TestRoot "stop.signal"
$log = Join-Path $TestRoot "runtime.log"
$stdout = Join-Path $TestRoot "stdout.log"
$stderr = Join-Path $TestRoot "stderr.log"
New-Item -ItemType Directory -Path $TestRoot -Force | Out-Null
Remove-Item -LiteralPath $signal, $log, $stdout, $stderr -Force -ErrorAction SilentlyContinue

$process = Start-Process -FilePath $Runtime `
  -ArgumentList @("--project", ('"' + $Project + '"'), "--stop-file", ('"' + $signal + '"'),
                  "--log-file", ('"' + $log + '"')) `
  -RedirectStandardOutput $stdout -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
$startupDeadline = [DateTime]::UtcNow.AddSeconds(30)
$startupObserved = $false
while ([DateTime]::UtcNow -lt $startupDeadline) {
  if ($process.HasExited) {
    throw "Runtime exited before accepting the stop request with code $($process.ExitCode)"
  }
  if (Test-Path -LiteralPath $log) {
    $startupLog = Get-Content -LiteralPath $log -Raw
    if ($startupLog -match "stage=application_create") {
      # 使用已观察到的状态，避免重复读取正在追加的日志产生瞬时空快照。
      $startupObserved = $true
      break
    }
  }
  Start-Sleep -Milliseconds 50
}
if (-not $startupObserved) {
  Stop-Process -Id $process.Id -Force
  throw "Runtime did not finish startup before the stop request"
}
New-Item -ItemType File -Path $signal -Force | Out-Null
$exitTimeoutMilliseconds = 15000
if (-not $process.WaitForExit($exitTimeoutMilliseconds)) {
  Stop-Process -Id $process.Id -Force
  $capturedStdout = if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw } else { "" }
  $capturedStderr = if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr -Raw } else { "" }
  throw "Runtime did not exit within ${exitTimeoutMilliseconds} ms after the stop request.`nstdout:`n$capturedStdout`nstderr:`n$capturedStderr"
}
if (-not (Test-Path -LiteralPath $log)) {
  throw "Runtime did not write the stop protocol log"
}
$content = Get-Content -LiteralPath $log -Raw
if ($content -notmatch "stage=stop_request" -or $content -notmatch "stage=shutdown") {
  throw "Runtime did not record stop_request and shutdown: $content"
}
