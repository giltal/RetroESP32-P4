#
# build_template.ps1 -- Build & upload the PAPP Template demo
#
# Usage: .\ESP32_P4_PAPP_Template\build_template.ps1 [-SkipUpload]
#

param([switch]$SkipUpload)

$ErrorActionPreference = "Stop"
$ROOT = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$AppName = "ESP32_P4_PAPP_Template"
$BUILD = Join-Path (Join-Path $ROOT "build_papp") $AppName
$FW = Join-Path $ROOT "firmware"
$HEADER = Join-Path (Join-Path (Join-Path $ROOT "components") "psram_app_loader") "include"
$LD = Join-Path (Join-Path $ROOT "tools") "psram_app.ld"
$SRC = Join-Path (Join-Path $ROOT "ESP32_P4_PAPP_Template") "main.c"

# Source ESP-IDF if not already done
if (-not (Get-Command "riscv32-esp-elf-gcc" -ErrorAction SilentlyContinue)) {
    $env:IDF_PYTHON_ENV_PATH = "C:\Users\97254\.espressif\python_env\idf5.5_py3.12_env"
    & "C:\Users\97254\esp\v5.5.2\esp-idf\export.ps1" 2>$null
}

New-Item -ItemType Directory -Force -Path $BUILD | Out-Null
New-Item -ItemType Directory -Force -Path $FW | Out-Null

# Compile
Write-Host "=== Building $AppName ===" -ForegroundColor Cyan
Write-Host "  CC  main.c"
$ccArgs = @(
    "-march=rv32imafc_zicsr_zifencei", "-mabi=ilp32f",
    "-mcmodel=medany", "-fno-common", "-ffunction-sections", "-fdata-sections",
    "-Os", "-Wall", "-DPAPP_APP_SIDE=1", "-I$HEADER",
    "-c", "-o", "$BUILD\main.o", $SRC
)
& riscv32-esp-elf-gcc @ccArgs
if ($LASTEXITCODE -ne 0) { throw "Compilation failed" }

# Link
Write-Host "  LD  $AppName.elf"
$ldArgs = @(
    "-march=rv32imafc_zicsr_zifencei", "-mabi=ilp32f",
    "-nostartfiles", "-nodefaultlibs", "-nostdlib",
    "-T$LD", "-Wl,--gc-sections", "-Wl,--entry=app_entry", "-Wl,--no-relax",
    "-o", "$BUILD\$AppName.elf", "$BUILD\main.o"
)
& riscv32-esp-elf-gcc @ldArgs
if ($LASTEXITCODE -ne 0) { throw "Linking failed" }

# Size
Write-Host "  SIZE:"
& riscv32-esp-elf-size "$BUILD\$AppName.elf"

# Objcopy
Write-Host "  OBJCOPY"
& riscv32-esp-elf-objcopy -O binary "$BUILD\$AppName.elf" "$BUILD\$AppName.bin"
if ($LASTEXITCODE -ne 0) { throw "objcopy failed" }

# Extract BSS size from ELF symbols
$nmOut = & riscv32-esp-elf-nm "$BUILD\$AppName.elf" | Select-String "bss"
$bssSize = 0
$bssStart = 0; $bssEnd = 0
foreach ($line in $nmOut) {
    if ($line -match '([0-9a-fA-F]+)\s+\S+\s+_bss_start') { $bssStart = [Convert]::ToUInt32($Matches[1], 16) }
    if ($line -match '([0-9a-fA-F]+)\s+\S+\s+_bss_end')   { $bssEnd   = [Convert]::ToUInt32($Matches[1], 16) }
}
if ($bssEnd -gt $bssStart) { $bssSize = $bssEnd - $bssStart }
Write-Host "  BSS size: $bssSize bytes"

# Pack
$pappPath = Join-Path $FW "$AppName.papp"
Write-Host "  PACK"
python (Join-Path (Join-Path $ROOT "tools") "pack_papp.py") "$BUILD\$AppName.bin" $pappPath --entry-offset 0 --bss-size $bssSize
if ($LASTEXITCODE -ne 0) { throw "pack_papp.py failed" }

Write-Host ""
Write-Host "=== $AppName.papp ready ===" -ForegroundColor Green

# Upload
if (-not $SkipUpload) {
    Write-Host "Uploading to device..." -ForegroundColor Cyan
    python (Join-Path (Join-Path $ROOT "tools") "upload_papp.py") $pappPath --port COM30
    if ($LASTEXITCODE -ne 0) { Write-Host "Upload failed (is device connected?)" -ForegroundColor Yellow }
}

Write-Host "Done!" -ForegroundColor Green
