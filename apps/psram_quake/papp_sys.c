/*
 * Quake System shim for PSRAM App — replaces sys_esp32.c + fatfs_proxy.
 *
 * All file I/O goes through app_services_t (the launcher's VFS layer).
 * No fatfs_proxy task needed — the launcher handles SD card access.
 *
 * Also replaces quake_main.c's fatfs_proxy_init/deinit with no-ops.
 */
#include "psram_app.h"
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>

#include "quakedef.h"

extern const app_services_t *_papp_svc;
extern volatile int papp_exit_requested;
extern void app_return_to_launcher(void);

#define MAX_HANDLES 32

typedef struct {
    bool isOpen;
    void *fp;           /* service table FILE* */
    uint32_t mmapOffset;
    bool isMmap;
} file_handle_t;

static file_handle_t sys_handles[MAX_HANDLES];

static bool sys_quit = false;

static const char *mmap_pak_path;
static uint32_t mmap_pak_size;
static const void *mmap_pak;

qboolean isDedicated = false;
extern qboolean r_fov_greater_than_90;

/* ── QUAKE_FILE wrapper (replaces fatfs_proxy calls) ─────────────────── */

struct QUAKE_FILE {
    void *fp;            /* service table FILE*, or NULL for mmap */
    bool isMemoryFile;
    const void *mmap_base;
    uint32_t mmap_size;
    uint32_t mmap_pos;
};

QUAKE_FILE *quake_fopen(const char *name, const char *type)
{
    void *fp = NULL;
    bool isMemoryFile = false;

    if (mmap_pak && mmap_pak_path && strcmp(name, mmap_pak_path) == 0) {
        if (strcmp(type, "rb") != 0) {
            _papp_svc->log_printf("quake_fopen: invalid mode %s for mmapped %s\n", type, name);
            return NULL;
        }
        isMemoryFile = true;
    } else {
        fp = _papp_svc->file_open(name, type);
        if (!fp) return NULL;
    }

    QUAKE_FILE *qfile = __wrap_malloc(sizeof(QUAKE_FILE));
    if (!qfile) {
        if (fp) _papp_svc->file_close(fp);
        return NULL;
    }

    qfile->fp = fp;
    qfile->isMemoryFile = isMemoryFile;
    qfile->mmap_base = mmap_pak;
    qfile->mmap_size = mmap_pak_size;
    qfile->mmap_pos = 0;

    return qfile;
}

int quake_fclose(QUAKE_FILE *qfile)
{
    int ret = 0;
    if (!qfile->isMemoryFile && qfile->fp)
        ret = _papp_svc->file_close(qfile->fp);
    __wrap_free(qfile);
    return ret;
}

int quake_fseek(QUAKE_FILE *qfile, long pos, int type)
{
    if (qfile->isMemoryFile) {
        switch (type) {
            case 0: /* SEEK_SET */ qfile->mmap_pos = pos; break;
            case 1: /* SEEK_CUR */ qfile->mmap_pos += pos; break;
            case 2: /* SEEK_END */ qfile->mmap_pos = qfile->mmap_size + pos; break;
        }
        return 0;
    }
    return _papp_svc->file_seek(qfile->fp, pos, type);
}

long quake_ftell(QUAKE_FILE *qfile)
{
    if (qfile->isMemoryFile) return (long)qfile->mmap_pos;
    return _papp_svc->file_tell(qfile->fp);
}

size_t quake_fread(void *buf, size_t size, size_t n, QUAKE_FILE *qfile)
{
    if (qfile->isMemoryFile) {
        size_t total = size * n;
        if (qfile->mmap_pos + total > qfile->mmap_size) {
            total = qfile->mmap_size - qfile->mmap_pos;
            n = total / size;
        }
        if (total > 0) {
            memcpy(buf, (const uint8_t *)qfile->mmap_base + qfile->mmap_pos, size * n);
            qfile->mmap_pos += size * n;
        }
        return n;
    }
    return _papp_svc->file_read(buf, size, n, qfile->fp);
}

size_t quake_fwrite(const void *buf, size_t size, size_t n, QUAKE_FILE *qfile)
{
    if (qfile->isMemoryFile) return 0;
    return _papp_svc->file_write(buf, size, n, qfile->fp);
}

int quake_fprintf(QUAKE_FILE *qfile, const char *fmt, ...)
{
    /* For save games — write formatted text to file */
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0 && qfile->fp)
        _papp_svc->file_write(buf, 1, len, qfile->fp);
    return len;
}

int quake_fscanf(QUAKE_FILE *qfile, const char *fmt, ...)
{
    /* For loading save games — read and parse. Simplified: read a line
       and use sscanf. */
    char buf[256];
    int idx = 0;
    while (idx < (int)sizeof(buf) - 1) {
        char c;
        size_t got;
        if (qfile->isMemoryFile) {
            if (qfile->mmap_pos >= qfile->mmap_size) break;
            c = ((const char *)qfile->mmap_base)[qfile->mmap_pos++];
        } else {
            got = _papp_svc->file_read(&c, 1, 1, qfile->fp);
            if (got == 0) break;
        }
        buf[idx++] = c;
        if (c == '\n') break;
    }
    buf[idx] = '\0';

    va_list args;
    va_start(args, fmt);
    int ret = vsscanf(buf, fmt, args);
    va_end(args);
    return ret;
}

int quake_fgetc(QUAKE_FILE *qfile)
{
    unsigned char c;
    if (qfile->isMemoryFile) {
        if (qfile->mmap_pos >= qfile->mmap_size) return -1;
        return ((const unsigned char *)qfile->mmap_base)[qfile->mmap_pos++];
    }
    size_t got = _papp_svc->file_read(&c, 1, 1, qfile->fp);
    return (got == 1) ? c : -1;
}

int quake_fflush(QUAKE_FILE *qfile)
{
    /* No explicit flush through service table */
    return 0;
}

int quake_feof(QUAKE_FILE *qfile)
{
    if (qfile->isMemoryFile)
        return qfile->mmap_pos >= qfile->mmap_size;
    /* Approximate: try to read 0 bytes or check position vs size */
    return 0;
}

/* ── Sys_File* — engine's file handle layer ──────────────────────────── */

static int findhandle(void)
{
    for (int i = 1; i < MAX_HANDLES; ++i)
        if (!sys_handles[i].isOpen)
            return i;
    Sys_Error("out of handles");
    return -1;
}

static int filelength_fp(void *fp)
{
    long pos = _papp_svc->file_tell(fp);
    _papp_svc->file_seek(fp, 0, 2 /* SEEK_END */);
    long end = _papp_svc->file_tell(fp);
    _papp_svc->file_seek(fp, pos, 0 /* SEEK_SET */);
    return (int)end;
}

int Sys_FileOpenRead(char *path, int *hndl)
{
    int i = findhandle();

    /* mmap hook */
    if (mmap_pak && mmap_pak_path && !strcmp(path, mmap_pak_path)) {
        sys_handles[i].isOpen = true;
        sys_handles[i].fp = NULL;
        sys_handles[i].isMmap = true;
        sys_handles[i].mmapOffset = 0;
        *hndl = i;
        return mmap_pak_size;
    }

    void *fp = _papp_svc->file_open(path, "rb");
    if (!fp) {
        *hndl = -1;
        return -1;
    }

    sys_handles[i].isOpen = true;
    sys_handles[i].fp = fp;
    sys_handles[i].isMmap = false;
    *hndl = i;

    return filelength_fp(fp);
}

int Sys_FileOpenWrite(char *path)
{
    int i = findhandle();

    void *fp = _papp_svc->file_open(path, "wb");
    if (!fp)
        Sys_Error("Error opening %s for write", path);

    sys_handles[i].isOpen = true;
    sys_handles[i].fp = fp;
    sys_handles[i].isMmap = false;

    return i;
}

void Sys_FileClose(int handle)
{
    if (sys_handles[handle].fp) {
        _papp_svc->file_close(sys_handles[handle].fp);
        sys_handles[handle].fp = NULL;
    }
    sys_handles[handle].isOpen = false;
}

void Sys_FileSeek(int handle, int position)
{
    if (!sys_handles[handle].isOpen) return;

    if (sys_handles[handle].isMmap) {
        sys_handles[handle].mmapOffset = position;
        return;
    }

    _papp_svc->file_seek(sys_handles[handle].fp, position, 0 /* SEEK_SET */);
}

int Sys_FileRead(int handle, void *dest, int count)
{
    if (!sys_handles[handle].isOpen) return 0;

    /* Yield periodically during heavy loading */
    _papp_svc->delay_ms(1);

    if (sys_handles[handle].isMmap) {
        if (sys_handles[handle].mmapOffset >= mmap_pak_size)
            return 0;
        if ((sys_handles[handle].mmapOffset + count) > mmap_pak_size)
            count = mmap_pak_size - sys_handles[handle].mmapOffset;
        memcpy(dest, (const uint8_t *)mmap_pak + sys_handles[handle].mmapOffset, count);
        sys_handles[handle].mmapOffset += count;
        return count;
    }

    return (int)_papp_svc->file_read(dest, 1, count, sys_handles[handle].fp);
}

int Sys_FileWrite(int handle, void *data, int count)
{
    if (!sys_handles[handle].isOpen || !sys_handles[handle].fp) return 0;
    return (int)_papp_svc->file_write(data, 1, count, sys_handles[handle].fp);
}

int Sys_FileTime(char *path)
{
    if (mmap_pak && mmap_pak_path && !strcmp(path, mmap_pak_path))
        return 1;

    void *fp = _papp_svc->file_open(path, "rb");
    if (fp) {
        _papp_svc->file_close(fp);
        return 1;
    }
    return -1;
}

void Sys_mkdir(char *path)
{
    /* mkdir stub — pretend success */
}

const void *Sys_FileGetMmapBase(int handle)
{
    if (!sys_handles[handle].isOpen) return NULL;
    return sys_handles[handle].isMmap ? mmap_pak : NULL;
}

/* ── System I/O ──────────────────────────────────────────────────────── */

void Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length)
{
}

void Sys_Error(char *error, ...)
{
    va_list argptr;
    char buf[256];
    va_start(argptr, error);
    vsnprintf(buf, sizeof(buf), error, argptr);
    va_end(argptr);

    if (_papp_svc)
        _papp_svc->log_printf("Quake Sys_Error: %s\n", buf);
    app_return_to_launcher();
    for (;;) {}
}

void Sys_Printf(char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    _papp_svc->log_printf("%s", buf);
}

void Sys_Quit(void)
{
    sys_quit = true;
    app_return_to_launcher();
}

double Sys_FloatTime(void)
{
    return ((uint64_t)_papp_svc->get_time_us()) / 1000000.0;
}

char *Sys_ConsoleInput(void) { return NULL; }
void Sys_Sleep(void) {}
void Sys_SendKeyEvents(void) {}
void Sys_HighFPPrecision(void) {}
void Sys_LowFPPrecision(void) {}

/* ── Quake main (replaces quake_main.c) ──────────────────────────────── */

static quakeparms_t parms;

void esp32_quake_main(int argc, char **argv, const char *basedir,
                      const char *pakPath, uint32_t pakSize, const void *pakMmap)
{
    mmap_pak_path = pakPath;
    mmap_pak_size = pakSize;
    mmap_pak = pakMmap;
    sys_quit = false;

    /* Set up save/config dirs */
    strcpy(com_savedir, "/sd/saves/quake");
    strcpy(com_configdir, "/sd/config/quake");

    /* 8MB Hunk for P4 */
    parms.memsize = 8192 * 1024;
    parms.membase = _papp_svc->mem_caps_alloc(parms.memsize, PAPP_MEM_CAP_SPIRAM);
    parms.basedir = (char *)basedir;

    if (parms.membase == NULL)
        Sys_Error("membase allocation failed");

    COM_InitArgv(argc, argv);

    parms.argc = com_argc;
    parms.argv = com_argv;

    _papp_svc->log_printf("QUAKE: Host_Init (hunk=%dKB at %p)\n",
                          parms.memsize / 1024, parms.membase);
    Host_Init(&parms);

    /* Deferred ambient precaching */
    extern void S_PrecacheAmbients(void);
    S_PrecacheAmbients();

    /* Force some cvars */
    Cvar_SetValue("r_drawviewmodel", 1);
    Cvar_SetValue("viewsize", 100);
    Cvar_SetValue("crosshair", 1);
    Cvar_SetValue("fov", 90);
    Cvar_SetValue("nosound", 0);
    Cvar_SetValue("gamma", 0.5);  /* max brightness (menu range 0.5-1.0, lower=brighter) */

    /* Bind custom controls */
    if (registered.value) {
        Cbuf_AddText("bind c +movedown\n");
    }

    double oldtime = Sys_FloatTime();
    int force_viewmodel = 100;

    while (!sys_quit && !papp_exit_requested) {
        double newtime = Sys_FloatTime();
        double time = newtime - oldtime;

        if (force_viewmodel > 0) {
            Cvar_SetValue("r_drawviewmodel", 1);
            r_fov_greater_than_90 = false;
            force_viewmodel--;
        }

        Host_Frame(time);

        _papp_svc->delay_ms(1);

        oldtime = newtime;
    }

    Host_Shutdown();
}

/* ── fatfs_proxy stubs (called by quake_main.c) ──────────────────────── */
/* These are no-ops because we replaced quake_main.c with esp32_quake_main
   above, but keep them in case any stray reference exists. */

void fatfs_proxy_init(void *ownerTask) {}
void fatfs_proxy_deinit(void) {}
