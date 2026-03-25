<#
.SYNOPSIS
    Build Quake (WinQuake) as a PSRAM .papp for RetroESP32-P4.

.DESCRIPTION
    Compiles the WinQuake engine files + PSRAM-app shim files,
    links against newlib + compiler-rt, wraps heap functions to route
    through app_services_t, produces quake.papp.
#>
param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ROOT = "C:\ESPIDFprojects\RetroESP32_P4_PSRAM"
$BUILD = "$ROOT\build_papp\quake"
$OUT   = "$ROOT\firmware\quake.papp"

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
    "-DESP32_QUAKE=1",
    "-DESP_PLATFORM=1",
    "-DCONFIG_IDF_TARGET_ESP32P4=1",
    "-DCONFIG_IDF_TARGET_ARCH_RISCV=1",
    "-Os",
    "-fcommon",
    "-ffunction-sections",
    "-fdata-sections",
    "-Wall",
    "-Wno-format",
    "-Wno-format-overflow",
    "-Wno-unused-variable",
    "-Wno-unused-function",
    "-Wno-maybe-uninitialized",
    "-Wno-implicit-function-declaration",
    "-Wno-pointer-sign",
    "-Wno-int-conversion",
    "-Wno-incompatible-pointer-types",
    "-Wno-dangling-pointer",
    "-Wno-dangling-else",
    "-Wno-array-bounds",
    "-Wno-address",
    "-Wno-restrict",
    "-Wno-builtin-declaration-mismatch",
    "-Wno-char-subscripts",
    "-Wno-sign-compare",
    "-Wno-parentheses",
    "-Wno-missing-braces",
    "-Wno-missing-field-initializers",
    "-Wno-trigraphs"
)

# Include paths — compat headers FIRST to override ESP-IDF/retro-go
$QUAKE_DIR = "$ROOT\components\quake"
$INCLUDES = @(
    "-I$ROOT\apps\psram_quake\compat",
    "-I$ROOT\components\psram_app_loader\include",
    "-I$QUAKE_DIR\winquake",
    "-I$QUAKE_DIR\esp32quake",
    "-I$QUAKE_DIR"
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

# WinQuake engine (from CMakeLists.txt — same set)
$WINQUAKE_SRCS = @(
    "chase.c","cmd.c","common.c","console.c","crc.c","cvar.c","draw.c",
    "host.c","host_cmd.c","keys.c","mathlib.c","menu.c","model.c",
    "nonintel.c","screen.c","sbar.c","zone.c","view.c","wad.c","world.c",
    "cl_demo.c","cl_input.c","cl_main.c","cl_parse.c","cl_tent.c",
    "d_edge.c","d_fill.c","d_init.c","d_modech.c","d_part.c",
    "d_polyse.c","d_scan.c","d_sky.c","d_sprite.c","d_surf.c",
    "d_vars.c","d_zpoint.c",
    "net_loop.c","net_main.c","net_vcr.c",
    "pr_cmds.c","pr_edict.c","pr_exec.c",
    "r_aclip.c","r_alias.c","r_bsp.c","r_light.c","r_draw.c",
    "r_efrag.c","r_edge.c","r_misc.c","r_main.c","r_sky.c",
    "r_sprite.c","r_surf.c","r_part.c","r_vars.c",
    "sv_main.c","sv_phys.c","sv_move.c","sv_user.c",
    "cd_null.c","net_none.c","snd_mem.c"
)

# PSRAM app shims (in apps/psram_quake/)
$PAPP_DIR = "$ROOT\apps\psram_quake"
$PAPP_SRCS = @(
    "papp_main.c",
    "papp_vid.c",
    "papp_snd.c",
    "papp_input.c",
    "papp_sys.c",
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

# Compile WinQuake engine sources
Write-Host "Compiling WinQuake engine ($($WINQUAKE_SRCS.Count) files)..." -ForegroundColor Cyan
foreach ($src in $WINQUAKE_SRCS) {
    $obj = "$BUILD\$($src -replace '\.c$','.o')"
    $srcpath = "$QUAKE_DIR\winquake\$src"
    $args = $ARCH_FLAGS + $CFLAGS + $INCLUDES + @("-c", "-o", $obj, $srcpath)
    $proc = Start-Process -FilePath $CC -ArgumentList $args -NoNewWindow -Wait -PassThru -RedirectStandardError "$BUILD\$src.err"
    if ($proc.ExitCode -ne 0) {
        Write-Host "  FAIL: $src" -ForegroundColor Red
        Get-Content "$BUILD\$src.err" | Select-Object -First 10
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
        Get-Content "$BUILD\$src.err" | Select-Object -First 10
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
$ELF = "$BUILD\quake.elf"
$link_args = $ARCH_FLAGS + $ALL_OBJS + $LDFLAGS + @("-o", $ELF)
$proc = Start-Process -FilePath $CC -ArgumentList $link_args -NoNewWindow -Wait -PassThru -RedirectStandardError "$BUILD\link.err"
if ($proc.ExitCode -ne 0) {
    Write-Host "  Link FAILED:" -ForegroundColor Red
    Get-Content "$BUILD\link.err" | Select-Object -First 30
    exit 1
}
Write-Host "  Linked: $ELF" -ForegroundColor Green

# Extract flat binary
$BIN = "$BUILD\quake.bin"
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
