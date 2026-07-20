$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $root "build_arm"
$stageDir = Join-Path $root "stage_usb"

if (-not (Test-Path (Join-Path $buildDir "tracker_demo"))) {
    throw "Missing build_arm\\tracker_demo. Run build_cross_arm.ps1 first."
}

if (Test-Path $stageDir) {
    Remove-Item -Recurse -Force $stageDir
}

New-Item -ItemType Directory -Path $stageDir | Out-Null

Copy-Item (Join-Path $buildDir "tracker_demo") $stageDir
Copy-Item (Join-Path $buildDir "fb_tracker_ui") $stageDir -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root "board_probe.sh") $stageDir
Copy-Item (Join-Path $root "run_cli_tests.sh") $stageDir
Copy-Item (Join-Path $root "run_linux_fb.sh") $stageDir
Copy-Item (Join-Path $root "DEPLOY_LINUX_FB.md") $stageDir

Write-Host "Staged files in:" $stageDir
