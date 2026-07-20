$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir = Join-Path $root "build"

if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$target = Join-Path $outDir "tracker_demo.exe"

$args = @(
  "-std=c11",
  "-O2",
  "-Wall",
  "-Wextra",
  "-pedantic",
  (Join-Path $root "main.c"),
  (Join-Path $root "tracker_app.c"),
  (Join-Path $root "tracker3d.c"),
  "-lm",
  "-o",
  $target
)

& gcc @args

if ($LASTEXITCODE -ne 0) {
    throw "Build failed."
}

Write-Host "Built:" $target
