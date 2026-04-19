# build_hdmi.ps1 — Build the launcher and all emulator apps for HDMI output
# Usage: .\build_hdmi.ps1
# Requires ESP-IDF environment to be active

$ErrorActionPreference = "Stop"

# Ensure ESP-IDF environment is loaded
$env:IDF_PYTHON_ENV_PATH = "C:\Users\97254\.espressif\python_env\idf5.5_py3.12_env"
& "C:\Users\97254\esp\v5.5.2\esp-idf\export.ps1" 2>$null

$ROOT = "C:\ESPIDFprojects\RetroESP32_P4"
$BINS = "$ROOT\firmware\hdmi"
New-Item -ItemType Directory -Path $BINS -Force | Out-Null

# HDMI sdkconfig overlay — merged with per-app sdkconfig.defaults
$HDMI_DEFAULTS = "$ROOT\launcher\sdkconfig.hdmi.defaults"

# ── Build Launcher (HDMI) ──────────────────────────────────────
Write-Host "`n=== Building Launcher (HDMI) ===" -ForegroundColor Cyan
Push-Location "$ROOT\launcher"
# Clean build directory to avoid stale LCD config
if (Test-Path "build") { Remove-Item -Recurse -Force "build" }
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.hdmi.defaults" build
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "Launcher HDMI build failed" }
Copy-Item "build\launcher.bin" "$BINS\launcher.bin" -Force
Copy-Item "build\bootloader\bootloader.bin" "$BINS\bootloader.bin" -Force
Copy-Item "build\partition_table\partition-table.bin" "$BINS\partition-table.bin" -Force
Copy-Item "build\ota_data_initial.bin" "$BINS\ota_data_initial.bin" -Force
Pop-Location
Write-Host "Launcher (HDMI): OK" -ForegroundColor Green

# ── Build Emulator Apps (HDMI) ─────────────────────────────────
$apps = @(
    @{ Name = "nes";        Dir = "apps\nes";       Bin = "nes_app.bin" },
    @{ Name = "gb";         Dir = "apps\gb";        Bin = "gb_app.bin" },
    @{ Name = "sms";        Dir = "apps\sms";       Bin = "sms_app.bin" },
    @{ Name = "spectrum";   Dir = "apps\spectrum";  Bin = "spectrum_app.bin" },
    @{ Name = "stella";     Dir = "apps\stella";    Bin = "stella_app.bin" },
    @{ Name = "prosystem";  Dir = "apps\prosystem"; Bin = "prosystem_app.bin" },
    @{ Name = "handy";      Dir = "apps\handy";     Bin = "handy_app.bin" },
    @{ Name = "pce";        Dir = "apps\pce";       Bin = "pce_app.bin" },
    @{ Name = "atari800";   Dir = "apps\atari800";  Bin = "atari800_app.bin" },
    @{ Name = "snes";       Dir = "apps\snes";      Bin = "snes_app.bin" },
    @{ Name = "genesis";    Dir = "apps\genesis";   Bin = "genesis_app.bin" }
)

foreach ($app in $apps) {
    Write-Host "`n=== Building $($app.Name) (HDMI) ===" -ForegroundColor Cyan
    Push-Location "$ROOT\$($app.Dir)"
    # Clean to avoid stale config
    if (Test-Path "build") { Remove-Item -Recurse -Force "build" }
    # Merge the app's own defaults with the HDMI overlay
    $appDefaults = "sdkconfig.defaults"
    if (Test-Path "sdkconfig.defaults") {
        idf.py -DSDKCONFIG_DEFAULTS="$appDefaults;$HDMI_DEFAULTS" build
    } else {
        idf.py -DSDKCONFIG_DEFAULTS="$HDMI_DEFAULTS" build
    }
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "$($app.Name) HDMI build failed" }
    Copy-Item "build\$($app.Bin)" "$BINS\$($app.Bin)" -Force
    Pop-Location
    Write-Host "$($app.Name) (HDMI): OK" -ForegroundColor Green
}

Write-Host "`n=== All HDMI builds complete! ===" -ForegroundColor Green
Write-Host "Binaries in: $BINS"
Get-ChildItem $BINS -Filter "*.bin" | Format-Table Name, @{N="Size(KB)";E={[math]::Round($_.Length/1024)}}
