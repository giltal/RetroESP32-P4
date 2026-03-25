<#
.SYNOPSIS
    Build Doom (prboom-go) as a PSRAM .papp for RetroESP32-P4.

.DESCRIPTION
    Compiles the 70 prboom engine files + 6 PSRAM-app shim files,
    links against newlib + compiler-rt, wraps heap functions to route
    through app_services_t, produces doom.papp.
#>
param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ROOT = "C:\ESPIDFprojects\RetroESP32_P4_PSRAM"
$BUILD = "$ROOT\build_papp\doom"
$OUT   = "$ROOT\firmware\doom.papp"

# Compiler
$CC  = "riscv32-esp-elf-gcc"
$OBJ = "riscv32-esp-elf-objcopy"

$ARCH_FLAGS = @(
    "-march=rv32imafc_zicsr_zifencei",
    "-mabi=ilp32f",
    "-mcmodel=medany"
)

$CFLAGS = @(
    "-DPAPP_APP_SIDE=1",
    "-DIRAM_ATTR=",
    "-DRETRO_GO=1",
    "-DHAVE_CONFIG_H=1",
    "-Os",
    "-fno-common",
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
    "-Wno-missing-braces"
)

# Include paths — compat headers FIRST to override ESP-IDF/retro-go
$PRBOOM_DIR = "$ROOT\components\prboom"
$INCLUDES = @(
    "-I$ROOT\apps\psram_doom\compat",
    "-I$ROOT\components\psram_app_loader\include",
    "-I$PRBOOM_DIR"
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
    "-Wl,--no-relax"
) + $WRAP_FLAGS + @("-lc", "-lgcc", "-lm")

# ── Source files ─────────────────────────────────────────────────────

# prboom engine (70 files)
$GAME_SRCS = @(
    "am_map.c","d_client.c","d_deh.c","d_items.c","d_main.c","d_server.c",
    "dbopl.c","doomdef.c","doomstat.c","dstrings.c",
    "f_finale.c","f_wipe.c","g_game.c",
    "hu_lib.c","hu_stuff.c","info.c","lprintf.c",
    "m_argv.c","m_bbox.c","m_cheat.c","m_menu.c","m_misc.c","m_random.c",
    "midifile.c","mus2mid.c","opl.c","opl_queue.c","oplplayer.c",
    "p_ceilng.c","p_doors.c","p_enemy.c","p_floor.c","p_genlin.c",
    "p_inter.c","p_lights.c","p_map.c","p_maputl.c","p_mobj.c",
    "p_plats.c","p_pspr.c","p_saveg.c","p_setup.c","p_sight.c",
    "p_spec.c","p_switch.c","p_telept.c","p_tick.c","p_user.c",
    "r_bsp.c","r_data.c","r_demo.c","r_draw.c","r_filter.c","r_fps.c",
    "r_main.c","r_patch.c","r_plane.c","r_segs.c","r_sky.c","r_things.c",
    "s_sound.c","sounds.c","st_lib.c","st_stuff.c","tables.c",
    "v_video.c","version.c","w_wad.c","wi_stuff.c","z_zone.c"
)

# PSRAM app shims (in apps/psram_doom/)
$PAPP_DIR = "$ROOT\apps\psram_doom"
$PAPP_SRCS = @(
    "papp_main.c",
    "papp_i_video.c",
    "papp_i_sound.c",
    "papp_i_system.c",
    "papp_rg_stubs.c",
    "papp_syscalls.c"
)

# ── Build ────────────────────────────────────────────────────────────

if ($Clean -and (Test-Path $BUILD)) {
    Remove-Item -Recurse -Force $BUILD
}
New-Item -ItemType Directory -Force -Path $BUILD | Out-Null

$ALL_OBJS = @()
$errors = 0

# Compile prboom engine sources
Write-Host "Compiling prboom engine ($($GAME_SRCS.Count) files)..." -ForegroundColor Cyan
foreach ($src in $GAME_SRCS) {
    $obj = "$BUILD\$($src -replace '\.c$','.o')"
    $srcpath = "$PRBOOM_DIR\$src"
    $args = $ARCH_FLAGS + $CFLAGS + $INCLUDES + @("-c", "-o", $obj, $srcpath)
    $proc = Start-Process -FilePath $CC -ArgumentList $args -NoNewWindow -Wait -PassThru -RedirectStandardError "$BUILD\$src.err"
    if ($proc.ExitCode -ne 0) {
        Write-Host "  FAIL: $src" -ForegroundColor Red
        Get-Content "$BUILD\$src.err" | Select-Object -First 5
        $errors++
    }
    $ALL_OBJS += $obj
}

# Compile PSRAM app shim sources
Write-Host "Compiling PSRAM app shims ($($PAPP_SRCS.Count) files)..." -ForegroundColor Cyan
foreach ($src in $PAPP_SRCS) {
    $obj = "$BUILD\$($src -replace '\.c$','.o')"
    $srcpath = "$PAPP_DIR\$src"
    $args = $ARCH_FLAGS + $CFLAGS + $INCLUDES + @("-c", "-o", $obj, $srcpath)
    $proc = Start-Process -FilePath $CC -ArgumentList $args -NoNewWindow -Wait -PassThru -RedirectStandardError "$BUILD\$src.err"
    if ($proc.ExitCode -ne 0) {
        Write-Host "  FAIL: $src" -ForegroundColor Red
        Get-Content "$BUILD\$src.err" | Select-Object -First 5
        $errors++
    }
    $ALL_OBJS += $obj
}

if ($errors -gt 0) {
    Write-Host "`n$errors file(s) failed to compile. Aborting." -ForegroundColor Red
    exit 1
}

Write-Host "Compiled $($ALL_OBJS.Count) object files." -ForegroundColor Green

# Link
Write-Host "Linking..." -ForegroundColor Cyan
$ELF = "$BUILD\doom.elf"
$link_args = $ARCH_FLAGS + $ALL_OBJS + $LDFLAGS + @("-o", $ELF)
$proc = Start-Process -FilePath $CC -ArgumentList $link_args -NoNewWindow -Wait -PassThru -RedirectStandardError "$BUILD\link.err"
if ($proc.ExitCode -ne 0) {
    Write-Host "  Link FAILED:" -ForegroundColor Red
    Get-Content "$BUILD\link.err" | Select-Object -First 30
    exit 1
}
Write-Host "  Linked: $ELF" -ForegroundColor Green

# Extract flat binary
$BIN = "$BUILD\doom.bin"
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
