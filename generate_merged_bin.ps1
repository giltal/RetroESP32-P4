# generate_merged_bin.ps1 - Combine all firmware binaries into a single flashable image
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

$ROOT = "C:\ESPIDFprojects\RetroESP32_P4"
$BINS = "$ROOT\firmware"
$OUT  = "$ROOT\RetroESP32_P4_v1.bin"

# Flash map (must match partitions_ota.csv)
# Format: offset, filename, description
$offsets = @("0x2000","0x8000","0xD000","0x10000","0x0D0000","0x160000","0x200000","0x350000","0x410000","0x550000","0x5F0000","0x690000","0x730000","0x8C0000","0x9B0000")
$files   = @("bootloader.bin","partition-table.bin","ota_data_initial.bin","launcher.bin","nes_app.bin","gb_app.bin","sms_app.bin","spectrum_app.bin","stella_app.bin","prosystem_app.bin","handy_app.bin","pce_app.bin","atari800_app.bin","snes_app.bin","genesis_app.bin")
$descs   = @("Bootloader","Partition Table","OTA Data","Launcher (factory)","NES (ota_0)","GB/GBC (ota_1)","SMS/GG/COL (ota_2)","ZX Spectrum (ota_3)","Stella (ota_4)","ProSystem (ota_5)","Handy (ota_6)","PC Engine (ota_7)","Atari 800 (ota_8)","SNES (ota_10)","Genesis (ota_11)")

Write-Host ""
Write-Host "=== RetroESP32-P4 Merged Binary Generator ===" -ForegroundColor Cyan
Write-Host "Output: $OUT"
Write-Host ""

# Build merge_bin argument list
[System.Collections.ArrayList]$merge_args = @("--chip","esp32p4","merge_bin","--output",$OUT,"--flash_mode","dio","--flash_size","16MB","--flash_freq","80m")

$missing = @()
for ($i = 0; $i -lt $offsets.Count; $i++) {
    $binPath = Join-Path $BINS $files[$i]
    if (Test-Path $binPath) {
        $size_kb = [math]::Round((Get-Item $binPath).Length / 1024)
        Write-Host ("  {0}  {1}  ({2}KB)  {3}" -f $offsets[$i], $files[$i], $size_kb, $descs[$i]) -ForegroundColor Gray
        $null = $merge_args.Add($offsets[$i])
        $null = $merge_args.Add($binPath)
    } else {
        Write-Host "  MISSING: $($files[$i]) - $($descs[$i])" -ForegroundColor Yellow
        $missing += $descs[$i]
    }
}

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "WARNING: $($missing.Count) missing binaries skipped: $($missing -join ', ')" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Running merge_bin..." -ForegroundColor Cyan
& python -m esptool $merge_args

if ($LASTEXITCODE -eq 0) {
    $size_mb = [math]::Round((Get-Item $OUT).Length / 1024 / 1024, 2)
    Write-Host "" 
    Write-Host "=== SUCCESS ===" -ForegroundColor Green
    Write-Host "Output : $OUT"
    Write-Host "Size   : ${size_mb} MB"
    Write-Host "Flash  : address 0x00000000" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "To flash via esptool:" 
    Write-Host "  python -m esptool --chip esp32p4 -p COM30 -b 460800 write_flash 0x0 RetroESP32_P4_v1.bin" -ForegroundColor White
} else {
    Write-Host ""
    Write-Host "=== FAILED ===" -ForegroundColor Red
    exit 1
}
