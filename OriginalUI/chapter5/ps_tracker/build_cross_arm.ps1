$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir = Join-Path $root "build_arm"
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$prefix = if ($env:CROSS_PREFIX) { $env:CROSS_PREFIX } else { "arm-none-linux-gnueabihf-" }
$gcc = "${prefix}gcc"
$strip = "${prefix}strip"

function Find-Tool($toolName) {
    $cmd = Get-Command $toolName -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    return $null
}

$gccPath = Find-Tool $gcc
if (-not $gccPath) {
    throw "Cross compiler not found: $gcc. Set CROSS_PREFIX or add the toolchain bin directory to PATH."
}

$commonArgs = @(
    "-std=c11",
    "-O2",
    "-Wall",
    "-Wextra",
    "-pedantic",
    "-mfpu=vfpv3",
    "-mfloat-abi=hard",
    "-mcpu=cortex-a9",
    "-static"
)

$cliOut = Join-Path $outDir "tracker_demo"
$fbOut = Join-Path $outDir "fb_tracker_ui"

& $gcc @commonArgs `
    (Join-Path $root "main.c") `
    (Join-Path $root "tracker_app.c") `
    (Join-Path $root "tracker3d.c") `
    "-lm" `
    "-o" $cliOut

if ($LASTEXITCODE -ne 0) {
    throw "CLI cross build failed."
}

& $gcc @commonArgs `
    (Join-Path $root "fb_linux_main.c") `
    (Join-Path $root "tracker_app.c") `
    (Join-Path $root "tracker3d.c") `
    (Join-Path $root "ui_draw.c") `
    "-lm" `
    "-o" $fbOut

if ($LASTEXITCODE -ne 0) {
    throw "Framebuffer UI cross build failed."
}

$stripPath = Find-Tool $strip
if ($stripPath) {
    & $stripPath $cliOut
    & $stripPath $fbOut
}

Write-Host "Built CLI:" $cliOut
Write-Host "Built FB UI:" $fbOut
