# build_all.ps1 — Build the launcher and all emulator apps
# Usage: .\build_all.ps1
# Requires ESP-IDF environment to be active

$ErrorActionPreference = "Stop"

# Ensure ESP-IDF environment is loaded
$env:IDF_PYTHON_ENV_PATH = "C:\Users\97254\.espressif\python_env\idf5.5_py3.11_env"
& "C:\Users\97254\esp\v5.5.2\esp-idf\export.ps1" 2>$null

$ROOT = "C:\ESPIDFprojects\RetroESP32_P4"
$BINS = "$ROOT\firmware"
New-Item -ItemType Directory -Path $BINS -Force | Out-Null

# ── Build Launcher ──────────────────────────────────────────────
Write-Host "`n=== Building Launcher ===" -ForegroundColor Cyan
Push-Location "$ROOT\launcher"
idf.py build
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "Launcher build failed" }
Copy-Item "build\launcher.bin" "$BINS\launcher.bin" -Force
Copy-Item "build\bootloader\bootloader.bin" "$BINS\bootloader.bin" -Force
Copy-Item "build\partition_table\partition-table.bin" "$BINS\partition-table.bin" -Force
Copy-Item "build\ota_data_initial.bin" "$BINS\ota_data_initial.bin" -Force
Pop-Location
Write-Host "Launcher: OK" -ForegroundColor Green

# ── Build Emulator Apps ────────────────────────────────────────
$apps = @(
    @{ Name = "nes";        Dir = "apps\nes";       Bin = "nes_app.bin" },
    @{ Name = "gb";         Dir = "apps\gb";        Bin = "gb_app.bin" },
    @{ Name = "sms";        Dir = "apps\sms";       Bin = "sms_app.bin" },
    @{ Name = "spectrum";   Dir = "apps\spectrum";  Bin = "spectrum_app.bin" },
    @{ Name = "stella";     Dir = "apps\stella";    Bin = "stella_app.bin" },
    @{ Name = "prosystem";  Dir = "apps\prosystem"; Bin = "prosystem_app.bin" },
    @{ Name = "handy";      Dir = "apps\handy";     Bin = "handy_app.bin" },
    @{ Name = "pce";        Dir = "apps\pce";       Bin = "pce_app.bin" },
    @{ Name = "atari800"; Dir = "apps\atari800"; Bin = "atari800_app.bin" },
    @{ Name = "opentyrian"; Dir = "apps\opentyrian"; Bin = "opentyrian_app.bin" },
    @{ Name = "snes";       Dir = "apps\snes";       Bin = "snes_app.bin" },
    @{ Name = "genesis";    Dir = "apps\genesis";    Bin = "genesis_app.bin" }
)

foreach ($app in $apps) {
    Write-Host "`n=== Building $($app.Name) ===" -ForegroundColor Cyan
    Push-Location "$ROOT\$($app.Dir)"
    idf.py build
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "$($app.Name) build failed" }
    Copy-Item "build\$($app.Bin)" "$BINS\$($app.Bin)" -Force
    Pop-Location
    Write-Host "$($app.Name): OK" -ForegroundColor Green
}

Write-Host "`n=== All builds complete! ===" -ForegroundColor Green
Write-Host "Binaries in: $BINS"
Get-ChildItem $BINS -Filter "*.bin" | Format-Table Name, @{N="Size(KB)";E={[math]::Round($_.Length/1024)}}

# ── Generate merged all-in-one binary ─────────────────────────
Write-Host "`n=== Generating merged binary ===" -ForegroundColor Cyan
& "$ROOT\generate_merged_bin.ps1"
