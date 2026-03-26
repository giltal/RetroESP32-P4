<#
.SYNOPSIS
    Build Duke Nukem 3D as a PSRAM .papp for RetroESP32-P4.

.DESCRIPTION
    Compiles the BUILD engine (10 files), Game logic (19 files),
    audiolib (11 files) and 6 PSRAM-app shim files, links against
    newlib + compiler-rt, produces duke3d.papp.
#>
param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ROOT  = "C:\ESPIDFprojects\RetroESP32_P4"
$BUILD = "$ROOT\build_papp\duke3d"
$OUT   = "$ROOT\firmware\duke3d.papp"

# Compiler
$CC  = "riscv32-esp-elf-gcc"

$ARCH_FLAGS = @(
    "-march=rv32imafc_zicsr_zifencei",
    "-mabi=ilp32f",
    "-mcmodel=medany"
)

$CFLAGS = @(
    "-DPAPP_APP_SIDE=1",
    "-DIRAM_ATTR=",
    "-DEXT_RAM_ATTR=",
    "-DPLATFORM_SUPPORTS_SDL=1",
    "-DPLATFORM_ESP32=1",
    "-DSTUB_NETWORKING=1",
    "-Os",
    "-fcommon",
    "-ffunction-sections",
    "-fdata-sections",
    "-Wall",
    "-Wno-format",
    "-Wno-unused-variable",
    "-Wno-unused-function",
    "-Wno-maybe-uninitialized",
    "-Wno-implicit-function-declaration",
    "-Wno-pointer-sign",
    "-Wno-int-conversion",
    "-Wno-incompatible-pointer-types",
    "-Wno-dangling-pointer",
    "-Wno-array-bounds",
    "-Wno-address",
    "-Wno-restrict",
    "-Wno-builtin-declaration-mismatch",
    "-Wno-char-subscripts",
    "-Wno-sign-compare",
    "-Wno-parentheses",
    "-Wno-missing-braces",
    "-Wno-return-type",
    "-Wno-stringop-overflow",
    "-Wno-stringop-truncation"
)

$DUKE3D_DIR = "$ROOT\components\duke3d"

# Include paths — compat headers FIRST to override ESP-IDF headers
$INCLUDES = @(
    "-I$ROOT\apps\psram_duke3d\compat",
    "-I$ROOT\components\psram_app_loader\include",
    "-I$DUKE3D_DIR\Engine",
    "-I$DUKE3D_DIR\Game",
    "-I$DUKE3D_DIR\audiolib"
)

# Linker script and flags
$LD_SCRIPT = "$ROOT\tools\psram_app.ld"
$WRAP_FLAGS = @(
    "-Wl,--wrap=malloc",
    "-Wl,--wrap=free",
    "-Wl,--wrap=calloc",
    "-Wl,--wrap=realloc",
    "-Wl,--wrap=_malloc_r",
    "-Wl,--wrap=_free_r",
    "-Wl,--wrap=_calloc_r",
    "-Wl,--wrap=_realloc_r",
    "-Wl,--wrap=__retarget_lock_init",
    "-Wl,--wrap=__retarget_lock_init_recursive",
    "-Wl,--wrap=__retarget_lock_close",
    "-Wl,--wrap=__retarget_lock_close_recursive",
    "-Wl,--wrap=__retarget_lock_acquire",
    "-Wl,--wrap=__retarget_lock_try_acquire",
    "-Wl,--wrap=__retarget_lock_acquire_recursive",
    "-Wl,--wrap=__retarget_lock_try_acquire_recursive",
    "-Wl,--wrap=__retarget_lock_release",
    "-Wl,--wrap=__retarget_lock_release_recursive"
)
$LDFLAGS = @(
    "-nostartfiles",
    "-nodefaultlibs",
    "-T$LD_SCRIPT",
    "-Wl,--gc-sections",
    "-Wl,--entry=app_entry",
    "-Wl,--no-relax",
    "-Wl,--allow-multiple-definition"
) + $WRAP_FLAGS + @("-lc", "-lgcc", "-lm")

# ── Source files ─────────────────────────────────────────────────────

# BUILD Engine (10 files)
$ENGINE_SRCS = @(
    "cache.c", "display.c", "draw.c", "dummy_multi.c",
    "engine.c", "filesystem.c", "fixedPoint_math.c",
    "tiles.c"
)

# Game logic (19 files)
$GAME_SRCS = @(
    "actors.c", "animlib.c", "config.c", "console.c",
    "control.c", "cvar_defs.c", "cvars.c",
    "game.c", "gamedef.c", "global.c", "keyboard.c",
    "menues.c", "player.c", "premap.c", "rts.c",
    "scriplib.c", "sector.c", "sounds.c",
    "dummy_audiolib.c"
)

# Audiolib — portable subset only (11 files, matching ESP32 component.mk)
$AUDIO_SRCS = @(
    "fx_man.c", "dsl.c", "ll_man.c", "multivoc.c",
    "mv_mix.c", "mvreverb.c", "nodpmi.c", "pitch.c",
    "user.c", "nomusic.c"
)

# PSRAM app shims (6 files)
$PAPP_DIR = "$ROOT\apps\psram_duke3d"
$PAPP_SRCS = @(
    "papp_main.c",
    "papp_syscalls.c",
    "papp_sdl_video.c",
    "papp_sdl_audio.c",
    "papp_sdl_event.c",
    "papp_sdl_system.c"
)

# ── Build ────────────────────────────────────────────────────────────

if ($Clean -and (Test-Path $BUILD)) {
    Remove-Item -Recurse -Force $BUILD
}
New-Item -ItemType Directory -Force -Path $BUILD | Out-Null

$ALL_OBJS = @()
$errors = 0

function Compile-Sources {
    param(
        [string]$Label,
        [string]$SrcDir,
        [string[]]$Sources,
        [string]$ObjPrefix = ""
    )

    Write-Host "Compiling $Label ($($Sources.Count) files)..." -ForegroundColor Cyan
    foreach ($src in $Sources) {
        $objName = "$ObjPrefix$($src -replace '\.c$','.o')"
        $obj = "$BUILD\$objName"
        $srcpath = "$SrcDir\$src"
        $args = $ARCH_FLAGS + $CFLAGS + $INCLUDES + @("-c", "-o", $obj, $srcpath)
        $proc = Start-Process -FilePath $CC -ArgumentList $args -NoNewWindow -Wait -PassThru -RedirectStandardError "$BUILD\$objName.err"
        if ($proc.ExitCode -ne 0) {
            Write-Host "  FAIL: $src" -ForegroundColor Red
            Get-Content "$BUILD\$objName.err" | Select-Object -First 10
            $script:errors++
        } else {
            $errContent = Get-Content "$BUILD\$objName.err" -Raw
            if ($errContent) {
                # Show warnings but don't fail
            }
        }
        $script:ALL_OBJS += $obj
    }
}

Compile-Sources -Label "BUILD Engine" -SrcDir "$DUKE3D_DIR\Engine" -Sources $ENGINE_SRCS -ObjPrefix "eng_"
Compile-Sources -Label "Game"         -SrcDir "$DUKE3D_DIR\Game"   -Sources $GAME_SRCS   -ObjPrefix "game_"
Compile-Sources -Label "Audiolib"     -SrcDir "$DUKE3D_DIR\audiolib" -Sources $AUDIO_SRCS -ObjPrefix "aud_"
Compile-Sources -Label "PSRAM shims"  -SrcDir $PAPP_DIR           -Sources $PAPP_SRCS   -ObjPrefix "papp_"

if ($errors -gt 0) {
    Write-Host "`n$errors file(s) failed to compile. Aborting." -ForegroundColor Red
    exit 1
}

Write-Host "Compiled $($ALL_OBJS.Count) object files." -ForegroundColor Green

# Link
Write-Host "Linking..." -ForegroundColor Cyan
$ELF = "$BUILD\duke3d.elf"
$link_args = $ARCH_FLAGS + $ALL_OBJS + $LDFLAGS + @("-o", $ELF)
$proc = Start-Process -FilePath $CC -ArgumentList $link_args -NoNewWindow -Wait -PassThru -RedirectStandardError "$BUILD\link.err"
if ($proc.ExitCode -ne 0) {
    Write-Host "  Link FAILED:" -ForegroundColor Red
    Get-Content "$BUILD\link.err" | Select-Object -First 40
    exit 1
}
Write-Host "  Linked: $ELF" -ForegroundColor Green

# Extract flat binary
$BIN = "$BUILD\duke3d.bin"
& riscv32-esp-elf-objcopy -O binary $ELF $BIN
if (-not (Test-Path $BIN)) {
    Write-Host "  objcopy FAILED" -ForegroundColor Red
    exit 1
}
$binSize = (Get-Item $BIN).Length
Write-Host "  Binary: $BIN ($binSize bytes)" -ForegroundColor Green

# Extract BSS size from ELF
$NM = "riscv32-esp-elf-nm"
$nmOut = & $NM $ELF 2>$null
$bssEndLine = $nmOut | Where-Object { $_ -match '\b_bss_end\b' }
if ($bssEndLine) {
    $bssEnd = [Convert]::ToUInt64(($bssEndLine.Trim() -split '\s+')[0], 16)
    $linkBase = 0x4A000000
    $binaryEndAddr = $linkBase + $binSize
    $bssToZero = [Math]::Max(0, $bssEnd - $binaryEndAddr)
    Write-Host "  _bss_end=0x$($bssEnd.ToString('X')), binary covers 0x$($linkBase.ToString('X'))-0x$($binaryEndAddr.ToString('X'))" -ForegroundColor Gray
    Write-Host "  BSS to zero: $bssToZero bytes" -ForegroundColor Gray
} else {
    $bssToZero = 0
    Write-Host "  Warning: _bss_end not found in ELF; bss_size=0" -ForegroundColor Yellow
}

# Pack .papp
Write-Host "Packing .papp..." -ForegroundColor Cyan
python "$ROOT\tools\pack_papp.py" $BIN $OUT --bss-size $bssToZero
Write-Host "  Output: $OUT ($((Get-Item $OUT).Length) bytes)" -ForegroundColor Green

Write-Host "`nBuild complete! Copy $OUT to /sd/roms/papp/ on the SD card." -ForegroundColor Green
