param(
    [string]$OutputRoot = (Join-Path ([IO.Path]::GetTempPath()) "schwung-signalscope")
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$zigBin = if ($env:ZIG_BIN) {
    $env:ZIG_BIN
} else {
    "zig"
}

$buildDir = Join-Path $OutputRoot "build"
$distDir = Join-Path $OutputRoot "dist"
$moduleDir = Join-Path $distDir "signalscope"
$package = Join-Path $distDir "signalscope-module.tar.gz"

if (Test-Path $OutputRoot) {
    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $buildDir, $moduleDir | Out-Null

& $zigBin c++ `
    -target aarch64-linux-gnu `
    -shared -fPIC -O2 -std=c++14 -pthread `
    -I (Join-Path $repoRoot "src\dsp") `
    (Join-Path $repoRoot "src\dsp\signalscope.cpp") `
    -o (Join-Path $buildDir "dsp.so") `
    -ldl -lpthread -lrt -lm

Copy-Item (Join-Path $repoRoot "src\module.json") (Join-Path $moduleDir "module.json") -Force
Copy-Item (Join-Path $repoRoot "src\help.json") (Join-Path $moduleDir "help.json") -Force
Copy-Item (Join-Path $repoRoot "src\ui.js") (Join-Path $moduleDir "ui.js") -Force
Copy-Item (Join-Path $buildDir "dsp.so") (Join-Path $moduleDir "dsp.so") -Force
Copy-Item (Join-Path $repoRoot "scripts\signalscope-temperature.sh") (Join-Path $moduleDir "signalscope-temperature.sh") -Force

tar -czf $package -C $distDir signalscope
Write-Output $package
