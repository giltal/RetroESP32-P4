#
# build_psram_app.ps1 — Build a PSRAM app (.papp) from C source files
#
# Usage:
#   .\tools\build_psram_app.ps1 -AppName psram_test -Sources apps\psram_test\main.c
#
# Prerequisites:
#   - ESP-IDF environment sourced (provides riscv32-esp-elf toolchain)
#   - tools\psram_app.ld linker script
#   - tools\pack_papp.py packer script
#
# Output:
#   firmware\<AppName>.papp   — ready to copy to SD card /sd/apps/
#

param(
    [Parameter(Mandatory=$true)]
    [string]$AppName,

    [Parameter(Mandatory=$true)]
    [string[]]$Sources,

    [string]$ExtraIncludes = "",
    [int]$EntryOffset = 0
)

$ErrorActionPreference = "Stop"

$ROOT      = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$TOOLS_DIR = Join-Path $ROOT "tools"
$BUILD_DIR = Join-Path $ROOT "build_papp" $AppName
$FW_DIR    = Join-Path $ROOT "firmware"
$LD_SCRIPT = Join-Path $TOOLS_DIR "psram_app.ld"
$PACKER    = Join-Path $TOOLS_DIR "pack_papp.py"
$HEADER    = Join-Path $ROOT "components" "psram_app_loader" "include"

# Toolchain (should be on PATH after sourcing ESP-IDF export.ps1)
$CC      = "riscv32-esp-elf-gcc"
$OBJCOPY = "riscv32-esp-elf-objcopy"
$OBJDUMP = "riscv32-esp-elf-objdump"
$SIZE    = "riscv32-esp-elf-size"

# ESP32-P4 RISC-V flags (must match ESP-IDF ABI)
$ARCH_FLAGS = @(
    "-march=rv32imafc_zicsr_zifencei",
    "-mabi=ilp32f"
)

# Compilation flags
$CFLAGS = @(
    "-mcmodel=medany",         # PC-relative addressing (position-independent)
    "-fno-common",             # No common symbols
    "-ffunction-sections",     # Each function in its own section
    "-fdata-sections",         # Each data item in its own section
    "-Os",                     # Optimize for size
    "-Wall",
    "-DPAPP_APP_SIDE=1",       # Exclude loader API from psram_app.h
    "-I$HEADER"
) + $ARCH_FLAGS

# Linker flags
$LDFLAGS = @(
    "-nostartfiles",           # No C runtime startup
    "-nodefaultlibs",          # No default libraries
    "-nostdlib",               # No standard library
    "-T$LD_SCRIPT",            # Our PSRAM app linker script
    "-Wl,--gc-sections",       # Remove unused sections
    "-Wl,--entry=app_entry",   # Entry point symbol
    "-Wl,--no-relax"           # Disable linker relaxation (keeps PC-relative)
) + $ARCH_FLAGS

# Add extra include paths
if ($ExtraIncludes) {
    $ExtraIncludes.Split(";") | ForEach-Object {
        $CFLAGS += "-I$_"
    }
}

# Create build directory
New-Item -ItemType Directory -Force -Path $BUILD_DIR | Out-Null
New-Item -ItemType Directory -Force -Path $FW_DIR | Out-Null

Write-Host "=== Building PSRAM App: $AppName ===" -ForegroundColor Cyan

# Step 1: Compile each source file
$Objects = @()
foreach ($src in $Sources) {
    $srcFull = if ([System.IO.Path]::IsPathRooted($src)) { $src } else { Join-Path $ROOT $src }
    $objName = [System.IO.Path]::GetFileNameWithoutExtension($src) + ".o"
    $objPath = Join-Path $BUILD_DIR $objName

    Write-Host "  CC  $src"
    $allFlags = $CFLAGS + @("-c", "-o", $objPath, $srcFull)
    & $CC @allFlags
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $src" }
    $Objects += $objPath
}

# Step 2: Link
$elfPath = Join-Path $BUILD_DIR "$AppName.elf"
Write-Host "  LD  $AppName.elf"
$allLdFlags = $LDFLAGS + @("-o", $elfPath) + $Objects
& $CC @allLdFlags
if ($LASTEXITCODE -ne 0) { throw "Linking failed" }

# Step 3: Show size info
Write-Host "  SIZE:"
& $SIZE $elfPath

# Step 4: Disassemble (for debugging)
$asmPath = Join-Path $BUILD_DIR "$AppName.asm"
& $OBJDUMP -d -S $elfPath | Out-File -FilePath $asmPath -Encoding utf8
Write-Host "  Disassembly: $asmPath"

# Step 5: Extract flat binary
$binPath = Join-Path $BUILD_DIR "$AppName.bin"
Write-Host "  OBJCOPY → $AppName.bin"
& $OBJCOPY -O binary $elfPath $binPath
if ($LASTEXITCODE -ne 0) { throw "objcopy failed" }

$binSize = (Get-Item $binPath).Length
Write-Host "  Binary size: $binSize bytes"

# Step 6: Pack into .papp
$pappPath = Join-Path $FW_DIR "$AppName.papp"
Write-Host "  PACK → $AppName.papp"
python $PACKER $binPath $pappPath --entry-offset $EntryOffset
if ($LASTEXITCODE -ne 0) { throw "pack_papp.py failed" }

Write-Host ""
Write-Host "=== $AppName.papp ready ===" -ForegroundColor Green
Write-Host "  Copy to SD card: /sd/apps/$AppName.papp"
Write-Host ""
