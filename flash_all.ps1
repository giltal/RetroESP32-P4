# flash_all.ps1 — Flash all binaries to ESP32-P4 via COM30
# Usage: .\flash_all.ps1
# Requires ESP-IDF environment to be active

$ErrorActionPreference = "Stop"

# Ensure ESP-IDF environment is loaded
$env:IDF_PYTHON_ENV_PATH = "C:\Users\97254\.espressif\python_env\idf5.5_py3.12_env"
& "C:\Users\97254\esp\v5.5.2\esp-idf\export.ps1" 2>$null

$ROOT = "C:\ESPIDFprojects\RetroESP32_P4"
$BINS = "$ROOT\firmware"
$PORT = "COM30"

# Kill any python processes holding the port
Stop-Process -Name python -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# ── Partition table layout (must match partitions_ota.csv) ─────
# Name       Offset     Bin file
$flash_map = @(
    @{ Offset = "0x2000";   File = "bootloader.bin";      Desc = "Bootloader" },
    @{ Offset = "0x8000";   File = "partition-table.bin";  Desc = "Partition Table" },
    @{ Offset = "0xD000";   File = "ota_data_initial.bin"; Desc = "OTA Data (boot factory)" },
    @{ Offset = "0x10000";  File = "launcher.bin";         Desc = "Launcher (factory)" },
    @{ Offset = "0x0C0000"; File = "nes_app.bin";          Desc = "NES (ota_0)" },
    @{ Offset = "0x160000"; File = "gb_app.bin";           Desc = "GB/GBC (ota_1)" },
    @{ Offset = "0x200000"; File = "sms_app.bin";          Desc = "SMS/GG/COL (ota_2)" },
    @{ Offset = "0x350000"; File = "spectrum_app.bin";     Desc = "ZX Spectrum (ota_3)" },
    @{ Offset = "0x410000"; File = "stella_app.bin";       Desc = "Stella (ota_4)" },
    @{ Offset = "0x550000"; File = "prosystem_app.bin";    Desc = "ProSystem (ota_5)" },
    @{ Offset = "0x5F0000"; File = "handy_app.bin";        Desc = "Handy (ota_6)" },
    @{ Offset = "0x690000"; File = "pce_app.bin";          Desc = "PC Engine (ota_7)" },
    @{ Offset = "0x730000"; File = "atari800_app.bin";     Desc = "Atari 800 (ota_8)" },
    @{ Offset = "0x8C0000"; File = "snes_app.bin";         Desc = "SNES (ota_10)" },
    @{ Offset = "0x9B0000"; File = "genesis_app.bin";      Desc = "Genesis (ota_11)" }
)

# Build the esptool command arguments
$args_list = @(
    "--chip", "esp32p4",
    "-p", $PORT,
    "-b", "460800",
    "--before", "default_reset",
    "--after", "hard_reset",
    "write_flash",
    "--flash_mode", "dio",
    "--flash_size", "16MB",
    "--flash_freq", "80m"
)

$missing = @()
foreach ($entry in $flash_map) {
    $path = "$BINS\$($entry.File)"
    if (Test-Path $path) {
        $size_kb = [math]::Round((Get-Item $path).Length / 1024)
        Write-Host "  $($entry.Desc): $($entry.File) (${size_kb}KB) @ $($entry.Offset)" -ForegroundColor Cyan
        $args_list += $entry.Offset
        $args_list += $path
    } else {
        Write-Host "  $($entry.Desc): MISSING $($entry.File) — skipping" -ForegroundColor Yellow
        $missing += $entry.Desc
    }
}

if ($missing.Count -gt 0) {
    Write-Host "`nWARNING: Skipping $($missing.Count) missing binaries: $($missing -join ', ')" -ForegroundColor Yellow
}

Write-Host "`n=== Flashing to $PORT ===" -ForegroundColor Green
python -m esptool $args_list

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n=== Flash complete! ===" -ForegroundColor Green
} else {
    Write-Host "`n=== Flash FAILED ===" -ForegroundColor Red
    exit 1
}
