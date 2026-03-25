/*
 * Newlib syscall stubs + heap wrappers for PSRAM App.
 *
 * We link against the toolchain's -lc (newlib) for printf/sprintf/string/etc.
 * These stubs redirect heap and I/O syscalls through app_services_t.
 *
 * We use --wrap=malloc/free/calloc/realloc at link time so that ALL
 * calls (including from within newlib) go through our wrappers.
 */
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <reent.h>
#include "psram_app.h"

extern const app_services_t *_papp_svc;

/* ── Allocation tracker ──────────────────────────────────────────────── */
/*
 * Track every heap allocation so we can free all remaining ones on exit.
 * The PSRAM app loader does NOT free app heap allocations — only the
 * code buffer. Without tracking, leaked allocations accumulate across
 * runs and crash on the 2nd launch.
 */
#define ALLOC_TABLE_SIZE 8192
static void *alloc_table[ALLOC_TABLE_SIZE];
static int alloc_count = 0;

static void track_add(void *ptr)
{
    if (ptr && alloc_count < ALLOC_TABLE_SIZE)
        alloc_table[alloc_count++] = ptr;
}

static void track_remove(void *ptr)
{
    if (!ptr) return;
    for (int i = alloc_count - 1; i >= 0; i--) {
        if (alloc_table[i] == ptr) {
            alloc_table[i] = alloc_table[--alloc_count];
            return;
        }
    }
}

/* Free ALL tracked allocations — called during app exit cleanup */
void papp_cleanup_heap(void)
{
    if (_papp_svc) {
        _papp_svc->log_printf("DOOM cleanup: freeing %d tracked allocations\n", alloc_count);
        for (int i = 0; i < alloc_count; i++) {
            if (alloc_table[i])
                _papp_svc->mem_free(alloc_table[i]);
        }
    }
    alloc_count = 0;
}

/* ── Heap wrappers (intercepted via --wrap) ──────────────────────────── */

/*
 * Route allocations ≥ 1 KB to PSRAM to preserve scarce internal RAM
 * for DMA buffers (SD card, LCD, etc.).  Small allocations stay in
 * internal RAM for speed.
 */
#define SPIRAM_THRESHOLD 1024

static void *alloc_smart(size_t size)
{
    if (size >= SPIRAM_THRESHOLD)
        return _papp_svc->mem_caps_alloc(size, PAPP_MEM_CAP_SPIRAM);
    return _papp_svc->mem_alloc(size);
}

void *__wrap_malloc(size_t size)     { void *p = alloc_smart(size); track_add(p); return p; }
void  __wrap_free(void *ptr)         { track_remove(ptr); _papp_svc->mem_free(ptr); }
void *__wrap_calloc(size_t n, size_t s)
{
    size_t total = n * s;
    void *p = alloc_smart(total);
    if (p) __builtin_memset(p, 0, total);
    track_add(p);
    return p;
}
void *__wrap_realloc(void *p, size_t s)
{
    track_remove(p);
    void *np = _papp_svc->mem_realloc(p, s);
    track_add(np);
    return np;
}

/* Also wrap the _r (reentrant) versions that newlib uses internally */
void *__wrap__malloc_r(struct _reent *r, size_t size) { void *p = alloc_smart(size); track_add(p); return p; }
void  __wrap__free_r(struct _reent *r, void *ptr)     { track_remove(ptr); _papp_svc->mem_free(ptr); }
void *__wrap__calloc_r(struct _reent *r, size_t n, size_t s)
{
    size_t total = n * s;
    void *p = alloc_smart(total);
    if (p) __builtin_memset(p, 0, total);
    track_add(p);
    return p;
}
void *__wrap__realloc_r(struct _reent *r, void *p, size_t s)
{
    track_remove(p);
    void *np = _papp_svc->mem_realloc(p, s);
    track_add(np);
    return np;
}

/* heap_caps_malloc — used by shim for PSRAM allocations */
void *heap_caps_malloc(size_t size, uint32_t caps)
{
    void *p = _papp_svc->mem_caps_alloc(size, caps);
    track_add(p);
    return p;
}

void heap_caps_free(void *ptr)
{
    track_remove(ptr);
    _papp_svc->mem_free(ptr);
}

/* ── Newlib low-level I/O syscalls ───────────────────────────────────── */
/*
 * Route fd-level operations through the launcher's VFS via the service
 * table's FILE*-level API.  We keep a small fd→FILE* table (fds 3..MAX)
 * because newlib's fopen → _open_r → _read_r → _lseek_r chain requires
 * working fd-level calls.
 */

#define FD_TABLE_SIZE 16
#define FD_BASE       3   /* 0=stdin, 1=stdout, 2=stderr */

static void *fd_table[FD_TABLE_SIZE]; /* stores FILE* (as void*) */

/* Close all open file descriptors */
void papp_cleanup_fds(void)
{
    for (int i = 0; i < FD_TABLE_SIZE; i++) {
        if (fd_table[i]) {
            _papp_svc->file_close(fd_table[i]);
            fd_table[i] = NULL;
        }
    }
}

static int fd_alloc(void *fp)
{
    for (int i = 0; i < FD_TABLE_SIZE; i++) {
        if (fd_table[i] == NULL) {
            fd_table[i] = fp;
            return i + FD_BASE;
        }
    }
    return -1;
}

static void *fd_lookup(int fd)
{
    int idx = fd - FD_BASE;
    if (idx < 0 || idx >= FD_TABLE_SIZE) return NULL;
    return fd_table[idx];
}

static void fd_release(int fd)
{
    int idx = fd - FD_BASE;
    if (idx >= 0 && idx < FD_TABLE_SIZE) fd_table[idx] = NULL;
}

/* Map POSIX open flags to fopen mode string */
#include <fcntl.h>
static const char *flags_to_mode(int flags)
{
    int acc = flags & O_ACCMODE;
    if (acc == O_RDONLY)                        return "rb";
    if (acc == O_WRONLY && (flags & O_APPEND))  return "ab";
    if (acc == O_WRONLY)                        return "wb";
    if (acc == O_RDWR   && (flags & O_APPEND))  return "ab+";
    if (acc == O_RDWR   && (flags & O_CREAT))   return "wb+";
    if (acc == O_RDWR)                          return "rb+";
    return "rb";
}

/* _sbrk — newlib's heap needs this but we bypass newlib's malloc.
 * Return -1 to indicate failure. */
void *_sbrk(ptrdiff_t incr)
{
    (void)incr;
    return (void *)-1;
}

void *_sbrk_r(struct _reent *reent, ptrdiff_t incr)
{
    (void)reent; (void)incr;
    return (void *)-1;
}

/* _write — used by printf/puts to output characters.
 * For fd 1 (stdout) and 2 (stderr): silently discard.
 * USB Serial JTAG blocks when no monitor is connected, causing
 * massive slowdowns during game init (~30 printf calls).
 * For other fds, write via file service. */
ssize_t _write(int fd, const void *buf, size_t count)
{
    if (fd == 1 || fd == 2) {
        return (ssize_t)count;  /* discard — avoids USB JTAG blocking */
    }
    void *fp = fd_lookup(fd);
    if (fp && _papp_svc) {
        return (ssize_t)_papp_svc->file_write(buf, 1, count, fp);
    }
    errno = EBADF;
    return -1;
}

ssize_t _write_r(struct _reent *reent, int fd, const void *buf, size_t count)
{
    return _write(fd, buf, count);
}

/* _read — read from file descriptor via service table.
 * Must loop because the service's fread may return short (buffering/SD).
 * Newlib interprets any short _read as EOF and stops reading. */
ssize_t _read(int fd, void *buf, size_t count)
{
    void *fp = fd_lookup(fd);
    if (!fp || !_papp_svc) { errno = EBADF; return -1; }

    size_t total = 0;
    while (total < count) {
        size_t got = _papp_svc->file_read((uint8_t *)buf + total, 1,
                                          count - total, fp);
        if (got == 0) break;  /* real EOF or error */
        total += got;
    }
    return (ssize_t)total;
}

ssize_t _read_r(struct _reent *reent, int fd, void *buf, size_t count)
{
    return _read(fd, buf, count);
}

/* _open — open a file via the service table, assign an fd */
int _open(const char *path, int flags, int mode)
{
    if (!_papp_svc) { errno = ENOSYS; return -1; }
    const char *fmode = flags_to_mode(flags);
    void *fp = _papp_svc->file_open(path, fmode);
    if (!fp) { errno = ENOENT; return -1; }
    int fd = fd_alloc(fp);
    if (fd < 0) { _papp_svc->file_close(fp); errno = ENFILE; return -1; }
    return fd;
}

int _open_r(struct _reent *reent, const char *path, int flags, int mode)
{
    return _open(path, flags, mode);
}

/* _close — close a file fd */
int _close(int fd)
{
    void *fp = fd_lookup(fd);
    if (!fp) { errno = EBADF; return -1; }
    int ret = _papp_svc->file_close(fp);
    fd_release(fd);
    return ret;
}

int _close_r(struct _reent *reent, int fd)
{
    return _close(fd);
}

/* _lseek — seek within a file fd */
off_t _lseek(int fd, off_t offset, int whence)
{
    void *fp = fd_lookup(fd);
    if (!fp) { errno = EBADF; return (off_t)-1; }
    if (_papp_svc->file_seek(fp, (long)offset, whence) != 0) {
        errno = EINVAL;
        return (off_t)-1;
    }
    return (off_t)_papp_svc->file_tell(fp);
}

off_t _lseek_r(struct _reent *reent, int fd, off_t offset, int whence)
{
    return _lseek(fd, offset, whence);
}

/* _fstat — report file type and size.
 * Newlib's fseek(SEEK_END) uses st_size from _fstat, so we MUST
 * return the correct file size — uninitialized st_size = garbage
 * → fseek returns garbage → ftell returns garbage → Z_Malloc crash. */
int _fstat(int fd, struct stat *st)
{
    if (!st) return -1;
    __builtin_memset(st, 0, sizeof(*st));
    void *fp = fd_lookup(fd);
    if (fd >= FD_BASE && fp) {
        st->st_mode = S_IFREG;
        /* Compute file size by seeking to end and back */
        long cur = _papp_svc->file_tell(fp);
        _papp_svc->file_seek(fp, 0, 2 /* SEEK_END */);
        st->st_size = _papp_svc->file_tell(fp);
        _papp_svc->file_seek(fp, cur, 0 /* SEEK_SET */);
    } else {
        st->st_mode = S_IFCHR;
    }
    return 0;
}

int _fstat_r(struct _reent *reent, int fd, struct stat *st)
{
    return _fstat(fd, st);
}

/* _isatty */
int _isatty(int fd) { return (fd >= 0 && fd <= 2) ? 1 : 0; }
int _isatty_r(struct _reent *reent, int fd) { return _isatty(fd); }

/* _kill / _getpid — process stubs */
int _kill(int pid, int sig) { return -1; }
int _kill_r(struct _reent *reent, int pid, int sig) { return -1; }
int _getpid(void) { return 1; }
int _getpid_r(struct _reent *reent) { return 1; }

/* _gettimeofday — used by newlib for time functions */
#include <sys/time.h>
int _gettimeofday(struct timeval *tv, void *tz)
{
    if (tv && _papp_svc) {
        int64_t us = _papp_svc->get_time_us();
        tv->tv_sec = us / 1000000;
        tv->tv_usec = us % 1000000;
    }
    return 0;
}

/* _exit — trigger PSRAM app exit via longjmp back to app_entry */
void _exit(int status)
{
    extern void app_return_to_launcher(void);
    app_return_to_launcher();  /* longjmp → cleanup → return to launcher */
    for (;;) {} /* should not reach here */
}

/* ── ESP-IDF lock stubs (newlib retarget) ────────────────────────────── */
/* Intercepted via --wrap. Signatures must match newlib’s declarations. */
struct __lock;
void __wrap___retarget_lock_init(struct __lock **lock) { if (lock) *lock = (struct __lock *)0; }
void __wrap___retarget_lock_init_recursive(struct __lock **lock) { if (lock) *lock = (struct __lock *)0; }
void __wrap___retarget_lock_close(struct __lock *lock) {}
void __wrap___retarget_lock_close_recursive(struct __lock *lock) {}
void __wrap___retarget_lock_acquire(struct __lock *lock) {}
int  __wrap___retarget_lock_try_acquire(struct __lock *lock) { return 0; }
void __wrap___retarget_lock_acquire_recursive(struct __lock *lock) {}
int  __wrap___retarget_lock_try_acquire_recursive(struct __lock *lock) { return 0; }
void __wrap___retarget_lock_release(struct __lock *lock) {}
void __wrap___retarget_lock_release_recursive(struct __lock *lock) {}

/* ── Additional ESP-IDF stubs ────────────────────────────────────────── */

/* __getreent — ESP-IDF's newlib uses this for thread-local reentrancy struct.
 * Return the global _impure_ptr (single-threaded is fine for a PSRAM app). */
struct _reent *__getreent(void)
{
    return _impure_ptr;
}

/* mkdir — used by config save. Best effort via standard posix. */
#include <sys/stat.h>
int mkdir(const char *path, mode_t mode)
{
    /* The VFS layer in the launcher handles this if /sd/ is mounted */
    (void)mode;
    return 0; /* pretend success */
}

/* Some newlib code may call esp_rom_printf or similar. */
int esp_rom_printf(const char *fmt, ...) { return 0; }

/* abort() override */
void abort(void)
{
    if (_papp_svc)
        _papp_svc->log_printf("PAPP abort() called!\n");
    _exit(1);
}

/* ── Doom-specific POSIX stubs ───────────────────────────────────────── */

/* access — check if file exists. Doom uses this in D_AddFile/G_RecordDemo. */
int access(const char *path, int mode)
{
    if (!_papp_svc || !path) return -1;
    /* Try to open the file to check existence */
    void *fp = _papp_svc->file_open(path, "rb");
    if (fp) {
        _papp_svc->file_close(fp);
        return 0; /* file exists */
    }
    return -1; /* not found */
}

/* _stat — get file info. Return size via fseek/ftell. */
int _stat(const char *path, struct stat *st)
{
    if (!_papp_svc || !path || !st) return -1;
    void *fp = _papp_svc->file_open(path, "rb");
    if (!fp) return -1;
    _papp_svc->file_seek(fp, 0, 2); /* SEEK_END */
    long size = _papp_svc->file_tell(fp);
    _papp_svc->file_close(fp);
    __builtin_memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    st->st_size = size;
    return 0;
}

int _stat_r(struct _reent *reent, const char *path, struct stat *st)
{
    return _stat(path, st);
}

/* stat — non-underscore version */
int stat(const char *path, struct stat *st)
{
    return _stat(path, st);
}

/* _unlink — delete a file */
int _unlink(const char *path)
{
    (void)path;
    return -1; /* not supported */
}

int _unlink_r(struct _reent *reent, const char *path)
{
    return _unlink(path);
}

/* unlink — non-underscore */
int unlink(const char *path)
{
    return _unlink(path);
}

/* rename — Doom saves use this */
int _rename(const char *old_path, const char *new_path)
{
    (void)old_path; (void)new_path;
    return -1; /* not supported */
}

int rename(const char *old_path, const char *new_path)
{
    return _rename(old_path, new_path);
}
