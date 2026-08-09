#
# build_lvgl_papp.ps1 — Build the LVGL PSRAM app (.papp)
#
# LVGL is far too big for the generic build_psram_app.ps1 (which takes an
# explicit source list), so this script globs the LVGL tree, compiles it with
# the PAPP freestanding flags, caches the object files, and links everything
# against the app sources.
#
# Usage:
#   .\tools\build_lvgl_papp.ps1              # incremental (reuses LVGL objects)
#   .\tools\build_lvgl_papp.ps1 -Clean       # full rebuild
#
# Prerequisites:
#   - ESP-IDF environment sourced (riscv32-esp-elf toolchain on PATH)
#   - third_party\lvgl  (git clone --depth 1 -b release/v9.2 https://github.com/lvgl/lvgl.git)
#
# Output:
#   firmware\psram_lvgl.papp
#

param(
    [string]$AppName = "psram_lvgl",
    [switch]$Clean
)

# NOTE: deliberately NOT "Stop". Under PowerShell 5.1 any native tool writing to
# stderr (gcc warnings, ld's harmless RWX-segment notice) would otherwise become
# a terminating error. Every native invocation below checks $LASTEXITCODE and
# throws explicitly instead.
$ErrorActionPreference = "Continue"

$ROOT      = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$TOOLS_DIR = Join-Path $ROOT "tools"
. (Join-Path $TOOLS_DIR 'Resolve-IdfEnv.ps1')
Initialize-IdfEnv
$APP_DIR   = Join-Path (Join-Path $ROOT "apps") $AppName
$LVGL_DIR  = Join-Path (Join-Path $ROOT "third_party") "lvgl"
$BUILD_DIR = Join-Path (Join-Path $ROOT "build_papp") $AppName
$FW_DIR    = Join-Path $ROOT "firmware"
$LD_SCRIPT = Join-Path $TOOLS_DIR "psram_app.ld"
$PACKER    = Join-Path $TOOLS_DIR "pack_papp.py"
$PAPP_INC  = Join-Path (Join-Path (Join-Path $ROOT "components") "psram_app_loader") "include"

if (-not (Test-Path $LVGL_DIR)) {
    throw "LVGL not found at $LVGL_DIR`nRun: git clone --depth 1 -b release/v9.2 https://github.com/lvgl/lvgl.git `"$LVGL_DIR`""
}

$CC      = "riscv32-esp-elf-gcc"
$OBJCOPY = "riscv32-esp-elf-objcopy"
$SIZE    = "riscv32-esp-elf-size"

# ESP32-P4 RISC-V ABI — must match ESP-IDF
$ARCH_FLAGS = @("-march=rv32imafc_zicsr_zifencei", "-mabi=ilp32f")

# Freestanding compile flags.
#  -fno-tree-loop-distribute-patterns is ESSENTIAL: without it GCC rewrites the
#  byte loop inside our own memset()/memcpy() into a call to memset()/memcpy(),
#  i.e. infinite recursion.
$CFLAGS = @(
    "-mcmodel=medany",
    "-fno-common",
    "-ffunction-sections",
    "-fdata-sections",
    "-fno-tree-loop-distribute-patterns",
    "-ffreestanding",
    "-Os",
    "-DPAPP_APP_SIDE=1",
    "-DLV_CONF_INCLUDE_SIMPLE=1",
    "-I$PAPP_INC",
    "-I$APP_DIR",       # lv_conf.h lives here
    "-I$LVGL_DIR",      # for "lvgl/..." style includes
    "-I$(Join-Path $LVGL_DIR 'src')"
) + $ARCH_FLAGS

$LDFLAGS = @(
    "-nostartfiles",
    "-nodefaultlibs",
    "-nostdlib",
    "-T$LD_SCRIPT",
    "-Wl,--gc-sections",
    "-Wl,--entry=app_entry",
    "-Wl,--no-relax",
    # Our flat binary intentionally has one LOAD segment holding text+data,
    # which ld flags as RWX. That is expected for this model, so silence it.
    "-Wl,--no-warn-rwx-segments"
) + $ARCH_FLAGS

if ($Clean -and (Test-Path $BUILD_DIR)) {
    Write-Host "Cleaning $BUILD_DIR" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BUILD_DIR
}
New-Item -ItemType Directory -Force -Path $BUILD_DIR | Out-Null
New-Item -ItemType Directory -Force -Path $FW_DIR | Out-Null

Write-Host "=== Building LVGL PAPP: $AppName ===" -ForegroundColor Cyan

# ── Collect sources ──────────────────────────────────────────────────────────
# Everything under lvgl/src EXCEPT src\drivers (those pull in SDL/X11/Linux
# platform headers we neither have nor need — we supply our own display+indev).
$lvglSrc = Get-ChildItem -Path (Join-Path $LVGL_DIR "src") -Filter *.c -Recurse |
           Where-Object { $_.FullName -notmatch '\\src\\drivers\\' }

$appSrc = Get-ChildItem -Path $APP_DIR -Filter *.c

Write-Host ("  LVGL sources: {0}   App sources: {1}" -f $lvglSrc.Count, $appSrc.Count)

# ── Compile (cached: skip if .o is newer than .c) ────────────────────────────
$objects = @()
$compiled = 0
$skipped  = 0

function Compile-One($srcFile, $objPath, $tag) {
    $script:objects += $objPath
    if ((Test-Path $objPath) -and
        ((Get-Item $objPath).LastWriteTime -gt (Get-Item $srcFile).LastWriteTime)) {
        $script:skipped++
        return
    }
    $flags = $CFLAGS + @("-c", "-o", $objPath, $srcFile)
    & $CC @flags
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $srcFile" }
    $script:compiled++
    if ($script:compiled % 40 -eq 0) { Write-Host "    ... $($script:compiled) compiled" }
}

# LVGL objects go in a subdir with flattened, collision-safe names
$lvglObjDir = Join-Path $BUILD_DIR "lvgl"
New-Item -ItemType Directory -Force -Path $lvglObjDir | Out-Null

Write-Host "  CC  LVGL ..."
foreach ($f in $lvglSrc) {
    # Flatten path into a unique object name: src\core\lv_obj.c -> core_lv_obj.o
    $rel  = $f.FullName.Substring((Join-Path $LVGL_DIR "src").Length).TrimStart('\')
    $name = ($rel -replace '\\', '_') -replace '\.c$', '.o'
    Compile-One $f.FullName (Join-Path $lvglObjDir $name) "lvgl"
}

Write-Host "  CC  app ..."
foreach ($f in $appSrc) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($f.Name) + ".o"
    Compile-One $f.FullName (Join-Path $BUILD_DIR $name) "app"
}

Write-Host ("  compiled: {0}   cached: {1}" -f $compiled, $skipped) -ForegroundColor DarkGray

# ── Link ─────────────────────────────────────────────────────────────────────
# -lgcc supplies compiler helper routines (64-bit divide etc.). It is safe here
# because the linker script binds the app at its real runtime address
# (0x4A000000), so absolute addressing inside libgcc resolves correctly.
$elfPath = Join-Path $BUILD_DIR "$AppName.elf"
Write-Host "  LD  $AppName.elf"
$allLd = $LDFLAGS + @("-o", $elfPath) + $objects + @("-lgcc")
& $CC @allLd
if ($LASTEXITCODE -ne 0) { throw "Linking failed" }

Write-Host "  SIZE:"
& $SIZE $elfPath

# ── Flat binary + pack ───────────────────────────────────────────────────────
$binPath = Join-Path $BUILD_DIR "$AppName.bin"
& $OBJCOPY -O binary $elfPath $binPath
if ($LASTEXITCODE -ne 0) { throw "objcopy failed" }
$binSize = (Get-Item $binPath).Length
Write-Host ("  Binary size: {0} bytes" -f $binSize)

# ── .bss size (CRITICAL) ─────────────────────────────────────────────────────
# .bss is NOLOAD, so it is not in the flat binary. The loader allocates
# text+data+bss and zeroes the bss region — but only if we tell it how big
# .bss is. Get it wrong and the app writes past its allocation and corrupts
# the heap. LVGL alone puts ~256 KB (its malloc pool) in .bss.
# Derive it from the _bss_end linker symbol, exactly as build_doom_papp.ps1 does.
$nmOut = & "riscv32-esp-elf-nm" $elfPath 2>$null
$bssEndLine = $nmOut | Where-Object { $_ -match '\b_bss_end\b' }
if ($bssEndLine) {
    $bssEnd        = [Convert]::ToUInt64(($bssEndLine.Trim() -split '\s+')[0], 16)
    $linkBase      = 0x4A000000
    $binaryEndAddr = $linkBase + $binSize
    $bssToZero     = [Math]::Max(0, $bssEnd - $binaryEndAddr)
    Write-Host ("  BSS to zero: {0} bytes" -f $bssToZero) -ForegroundColor Gray
} else {
    throw "_bss_end not found in ELF - refusing to pack with bss_size=0 (would corrupt the heap)"
}

$pappPath = Join-Path $FW_DIR "$AppName.papp"
python $PACKER $binPath $pappPath --entry-offset 0 --bss-size $bssToZero
if ($LASTEXITCODE -ne 0) { throw "pack_papp.py failed" }

# ── Also stage into the SD-card folder tracked in the repo ───────────────────
$sdDir = Join-Path (Join-Path (Join-Path $ROOT "SDcard") "roms") "papp"
if (Test-Path $sdDir) {
    Copy-Item $pappPath (Join-Path $sdDir "$AppName.papp") -Force
    Write-Host "  Staged -> SDcard\roms\papp\$AppName.papp" -ForegroundColor DarkGray
}

Write-Host ""
Write-Host "=== $AppName.papp ready ===" -ForegroundColor Green
Write-Host "  Copy to SD card: /sd/roms/papp/$AppName.papp"
Write-Host ""
