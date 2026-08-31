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
Start-Sleep -Milliseconds 800
New-Item -ItemType File -Path $signal -Force | Out-Null
if (-not $process.WaitForExit(5000)) {
  Stop-Process -Id $process.Id -Force
  throw "Runtime did not exit after the stop request"
}
if (-not (Test-Path -LiteralPath $log)) {
  throw "Runtime did not write the stop protocol log"
}
$content = Get-Content -LiteralPath $log -Raw
if ($content -notmatch "stage=stop_request" -or $content -notmatch "stage=shutdown") {
  throw "Runtime did not record stop_request and shutdown: $content"
}
