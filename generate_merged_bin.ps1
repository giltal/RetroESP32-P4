# generate_merged_bin.ps1 — Combine all firmware binaries into a single flashable image
#
# Output: RetroESP32_P4_v1.bin  (flash at address 0x0000)
#
# Usage:
#   .\generate_merged_bin.ps1
#
# Flash with esptool:
#   python -m esptool --chip esp32p4 -p COM30 -b 460800 write_flash 0x0 RetroESP32_P4_v1.bin
#
# Flash with Espressif Flash Download Tool:
#   Select "ESP32-P4", add RetroESP32_P4_v1.bin at address 0x00000000, click START

$ErrorActionPreference = "Stop"

$ROOT = "C:\ESPIDFprojects\RetroESP32_P4_PSRAM"
$BINS = "$ROOT\firmware"
$OUT  = "$ROOT\RetroESP32_P4_v1.bin"
$idfPython = "C:\Users\97254\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"

# ── Flash map (must match partitions_ota.csv) ──────────────────
$flash_map = @(
    @{ Offset = "0x2000";   File = "bootloader.bin";      Desc = "Bootloader" },
    @{ Offset = "0x8000";   File = "partition-table.bin";  Desc = "Partition Table" },
    @{ Offset = "0xD000";   File = "ota_data_initial.bin"; Desc = "OTA Data (boot factory)" },
    @{ Offset = "0x10000";  File = "launcher.bin";         Desc = "Launcher (factory)" },
    @{ Offset = "0x0C0000"; File = "nes_app.bin";          Desc = "NES (ota_0)" },
    @{ Offset = "0x160000"; File = "gb_app.bin";           Desc = "GB/GBC (ota_1)" },
    @{ Offset = "0x200000"; File = "sms_app.bin";          Desc = "SMS/GG/COL (ota_2)" },
    @{ Offset = "0x350000"; File = "spectrum_app.bin";     Desc = "ZX Spectrum (ota_3)" },
    @{ Offset = "0x410000"; File = "stella_app.bin";       Desc = "Stella / Atari 2600 (ota_4)" },
    @{ Offset = "0x550000"; File = "prosystem_app.bin";    Desc = "ProSystem / Atari 7800 (ota_5)" },
    @{ Offset = "0x5F0000"; File = "handy_app.bin";        Desc = "Handy / Atari Lynx (ota_6)" },
    @{ Offset = "0x690000"; File = "pce_app.bin";          Desc = "PC Engine (ota_7)" },
    @{ Offset = "0x730000"; File = "atari800_app.bin";     Desc = "Atari 800 (ota_8)" },
    @{ Offset = "0x8C0000"; File = "snes_app.bin";         Desc = "SNES (ota_10)" },
    @{ Offset = "0x9B0000"; File = "genesis_app.bin";      Desc = "Genesis (ota_11)" }
)

Write-Host "`n=== RetroESP32-P4 Merged Binary Generator ===" -ForegroundColor Cyan
Write-Host "Output: $OUT`n"

# Build merge_bin argument list
$merge_args = @(
    "--chip", "esp32p4",
    "merge_bin",
    "--output", $OUT,
    "--flash_mode", "dio",
    "--flash_size", "16MB",
    "--flash_freq", "80m"
)

$missing = @()
foreach ($entry in $flash_map) {
    $path = "$BINS\$($entry.File)"
    if (Test-Path $path) {
        $size_kb = [math]::Round((Get-Item $path).Length / 1024)
        Write-Host "  $($entry.Offset)  $($entry.File)  (${size_kb}KB)  — $($entry.Desc)" -ForegroundColor Gray
        $merge_args += $entry.Offset
        $merge_args += $path
    } else {
        Write-Host "  MISSING: $($entry.File) — $($entry.Desc)" -ForegroundColor Yellow
        $missing += $entry.Desc
    }
}

if ($missing.Count -gt 0) {
    Write-Host "`nWARNING: $($missing.Count) missing binaries skipped: $($missing -join ', ')" -ForegroundColor Yellow
}

Write-Host "`nRunning merge_bin..." -ForegroundColor Cyan
& $idfPython -m esptool @merge_args

if ($LASTEXITCODE -eq 0) {
    $size_mb = [math]::Round((Get-Item $OUT).Length / 1024 / 1024, 2)
    Write-Host "`n=== SUCCESS ===" -ForegroundColor Green
    Write-Host "Output : $OUT"
    Write-Host "Size   : ${size_mb} MB"
    Write-Host "Flash  : address 0x00000000" -ForegroundColor Yellow
    Write-Host "`nTo flash via esptool:"
    Write-Host "  python -m esptool --chip esp32p4 -p COM30 -b 460800 write_flash 0x0 RetroESP32_P4_v1.bin" -ForegroundColor White
    Write-Host "`nTo flash via Espressif Flash Download Tool:"
    Write-Host "  Chip: ESP32-P4, Address: 0x00000000, File: RetroESP32_P4_v1.bin" -ForegroundColor White
} else {
    Write-Host "`n=== FAILED ===" -ForegroundColor Red
    exit 1
}
