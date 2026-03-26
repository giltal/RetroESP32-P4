/*
 * Newlib syscall stubs + heap wrappers for Duke3D PSRAM App.
 *
 * Redirects heap and I/O syscalls through app_services_t.
 * Uses --wrap=malloc/free/calloc/realloc at link time.
 *
 * Duke3D opens the GRP file + multiple maps/saves simultaneously,
 * so we use a 48-slot FD table (vs 16 for Doom).
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

void papp_cleanup_heap(void)
{
    if (_papp_svc) {
        _papp_svc->log_printf("Duke3D cleanup: freeing %d tracked allocations\n", alloc_count);
        for (int i = 0; i < alloc_count; i++) {
            if (alloc_table[i])
                _papp_svc->mem_free(alloc_table[i]);
        }
    }
    alloc_count = 0;
}

/* ── Heap wrappers (intercepted via --wrap) ──────────────────────────── */
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

/* Reentrant versions used internally by newlib */
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

/* heap_caps_malloc — used by Duke3D engine for PSRAM allocations */
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

/* ── File descriptor table ───────────────────────────────────────────── */
/*
 * Duke3D opens the 11MB GRP file plus many maps, config, save files
 * simultaneously. 48 slots gives ample room.
 */
#define FD_TABLE_SIZE 48
#define FD_BASE       3   /* 0=stdin, 1=stdout, 2=stderr */

static void *fd_table[FD_TABLE_SIZE];

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

/* ── Newlib I/O syscalls ─────────────────────────────────────────────── */

void *_sbrk(ptrdiff_t incr) { (void)incr; return (void *)-1; }
void *_sbrk_r(struct _reent *reent, ptrdiff_t incr) { (void)reent; (void)incr; return (void *)-1; }

ssize_t _write(int fd, const void *buf, size_t count)
{
    if (fd == 1 || fd == 2) {
        /* Forward to log_printf so we can see init output */
        if (_papp_svc && count > 0) {
            char tmp[256];
            size_t n = count < 255 ? count : 255;
            __builtin_memcpy(tmp, buf, n);
            tmp[n] = '\0';
            _papp_svc->log_printf("%s", tmp);
        }
        return (ssize_t)count;
    }
    void *fp = fd_lookup(fd);
    if (fp && _papp_svc)
        return (ssize_t)_papp_svc->file_write(buf, 1, count, fp);
    errno = EBADF;
    return -1;
}
ssize_t _write_r(struct _reent *r, int fd, const void *buf, size_t count) { return _write(fd, buf, count); }

ssize_t _read(int fd, void *buf, size_t count)
{
    void *fp = fd_lookup(fd);
    if (!fp || !_papp_svc) { errno = EBADF; return -1; }
    size_t total = 0;
    while (total < count) {
        size_t got = _papp_svc->file_read((uint8_t *)buf + total, 1, count - total, fp);
        if (got == 0) break;
        total += got;
    }
    return (ssize_t)total;
}
ssize_t _read_r(struct _reent *r, int fd, void *buf, size_t count) { return _read(fd, buf, count); }

int _open(const char *path, int flags, int mode)
{
    if (!_papp_svc) { errno = ENOSYS; return -1; }
    /* Skip SD card for relative paths — they are GRP items (sounds, maps).
       kopen4load tries open() before searching GRP; save the SD overhead. */
    if (path && path[0] != '/') { errno = ENOENT; return -1; }
    const char *fmode = flags_to_mode(flags);
    void *fp = _papp_svc->file_open(path, fmode);
    if (!fp) {
        if (flags & (O_WRONLY | O_RDWR))
            _papp_svc->log_printf("[IO] _open FAIL: '%s' mode='%s'\n", path, fmode);
        errno = ENOENT;
        return -1;
    }
    int fd = fd_alloc(fp);
    if (fd < 0) { _papp_svc->file_close(fp); errno = ENFILE; return -1; }
    if (flags & (O_WRONLY | O_RDWR))
        _papp_svc->log_printf("[IO] _open OK: '%s' mode='%s' fd=%d\n", path, fmode, fd);
    return fd;
}
int _open_r(struct _reent *r, const char *path, int flags, int mode) { return _open(path, flags, mode); }

int _close(int fd)
{
    void *fp = fd_lookup(fd);
    if (!fp) { errno = EBADF; return -1; }
    int ret = _papp_svc->file_close(fp);
    fd_release(fd);
    return ret;
}
int _close_r(struct _reent *r, int fd) { return _close(fd); }

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
off_t _lseek_r(struct _reent *r, int fd, off_t offset, int whence) { return _lseek(fd, offset, whence); }

int _fstat(int fd, struct stat *st)
{
    if (!st) return -1;
    __builtin_memset(st, 0, sizeof(*st));
    void *fp = fd_lookup(fd);
    if (fd >= FD_BASE && fp) {
        st->st_mode = S_IFREG;
        long cur = _papp_svc->file_tell(fp);
        _papp_svc->file_seek(fp, 0, 2);
        st->st_size = _papp_svc->file_tell(fp);
        _papp_svc->file_seek(fp, cur, 0);
    } else {
        st->st_mode = S_IFCHR;
    }
    return 0;
}
int _fstat_r(struct _reent *r, int fd, struct stat *st) { return _fstat(fd, st); }

int _isatty(int fd) { return (fd >= 0 && fd <= 2) ? 1 : 0; }
int _isatty_r(struct _reent *r, int fd) { return _isatty(fd); }

int _kill(int pid, int sig) { return -1; }
int _kill_r(struct _reent *r, int pid, int sig) { return -1; }
int _getpid(void) { return 1; }
int _getpid_r(struct _reent *r) { return 1; }

#include <sys/time.h>
int _gettimeofday(struct timeval *tv, void *tz)
{
    if (tv && _papp_svc) {
        int64_t us = _papp_svc->get_time_us();
        tv->tv_sec  = us / 1000000;
        tv->tv_usec = us % 1000000;
    }
    return 0;
}

void _exit(int status)
{
    extern void app_return_to_launcher(void);
    app_return_to_launcher();
    for (;;) {}
}

/* ── Lock stubs ──────────────────────────────────────────────────────── */
struct __lock;
void __wrap___retarget_lock_init(struct __lock **lock)           { if (lock) *lock = (struct __lock *)0; }
void __wrap___retarget_lock_init_recursive(struct __lock **lock) { if (lock) *lock = (struct __lock *)0; }
void __wrap___retarget_lock_close(struct __lock *lock)           {}
void __wrap___retarget_lock_close_recursive(struct __lock *lock) {}
void __wrap___retarget_lock_acquire(struct __lock *lock)          {}
int  __wrap___retarget_lock_try_acquire(struct __lock *lock)      { return 0; }
void __wrap___retarget_lock_acquire_recursive(struct __lock *lock) {}
int  __wrap___retarget_lock_try_acquire_recursive(struct __lock *lock) { return 0; }
void __wrap___retarget_lock_release(struct __lock *lock)          {}
void __wrap___retarget_lock_release_recursive(struct __lock *lock) {}

struct _reent *__getreent(void) { return _impure_ptr; }

#include <sys/stat.h>
int mkdir(const char *path, mode_t mode) { (void)mode; return 0; }

int esp_rom_printf(const char *fmt, ...) { return 0; }

/* ── Duke3D engine memory allocators ─────────────────────────────────── */
void *kkmalloc(int size) { return malloc(size); }
void kkfree(void *ptr) { free(ptr); }
void *kmalloc(int size) { return malloc(size); }

/* ── ESP-IDF heap stub ───────────────────────────────────────────────── */
void heap_caps_print_heap_info(unsigned int caps) { (void)caps; }

/* ── Networking stubs (mmulti.c excluded) ──────────────────────────────── */
void sendpacket(int other, const void *buf, int len) { (void)other; (void)buf; (void)len; }
void sendlogoff(void) {}
void sendlogon(void) {}
void setpackettimeout(int datession, int timout) { (void)datession; (void)timout; }
void genericmultifunction(int other, const void *buf, int len, int flag) { (void)other; (void)buf; (void)len; (void)flag; }
void initmultiplayers(int argc, char **argv, int dacession, int daession2, int daession3)
{ (void)argc; (void)argv; (void)dacession; (void)daession2; (void)daession3; }
void uninitmultiplayers(void) {}
int getpacket(int *from, void *buf) { (void)from; (void)buf; return 0; }
int getoutputcirclesize(void) { return 0; }
void flushpackets(void) {}

/* ── DOS console stub ─────────────────────────────────────────────────── */
int getch(void) { return 'y'; }

/* ── STUBBED debug macro used by control.c ────────────────────────────── */
void STUBBED(const char *msg) { (void)msg; }

/* ── Directory stubs (VFS not available in papp) ─────────────────────── */
#include <dirent.h>
DIR *opendir(const char *name) { (void)name; return NULL; }
struct dirent *readdir(DIR *dirp) { (void)dirp; return NULL; }
int closedir(DIR *dirp) { (void)dirp; return 0; }

/* ── SD card init — already mounted by launcher ──────────────────────── */
void SDL_InitSD(void) {}

/* ── Heap query for engine startup ───────────────────────────────────── */
int Z_AvailHeap(void) { return 8 * 1024 * 1024; }

/* ── ESP-IDF logging — variadic, just discard ────────────────────────── */
void ESP_LOGV(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
void ESP_LOGD(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
void ESP_LOGI(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
void ESP_LOGW(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
void ESP_LOGE(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }

/* ── Newlib _times syscall ───────────────────────────────────────────── */
#include <sys/times.h>
clock_t _times(struct tms *buf)
{
    clock_t ticks = 0;
    if (_papp_svc) {
        /* get_time_us returns microseconds; convert to clock ticks */
        ticks = (clock_t)(_papp_svc->get_time_us() / (1000000 / CLOCKS_PER_SEC));
    }
    if (buf) { buf->tms_utime = ticks; buf->tms_stime = 0; buf->tms_cutime = 0; buf->tms_cstime = 0; }
    return ticks;
}

void abort(void)
{
    if (_papp_svc) _papp_svc->log_printf("DUKE3D PAPP abort() called!\n");
    _exit(1);
}

int access(const char *path, int mode)
{
    /* For absolute paths (SD card), actually probe the file.
       For relative paths (GRP items like KICK_HIT.VOC), return -1 to
       avoid slow SD-card probing during sound precaching. */
    if (!_papp_svc || !path || path[0] != '/') return -1;
    void *fp = _papp_svc->file_open(path, "rb");
    if (fp) {
        _papp_svc->file_close(fp);
        return 0;
    }
    return -1;
}

int _stat(const char *path, struct stat *st)
{
    if (!_papp_svc || !path || !st) return -1;
    void *fp = _papp_svc->file_open(path, "rb");
    if (!fp) return -1;
    _papp_svc->file_seek(fp, 0, 2);
    long size = _papp_svc->file_tell(fp);
    _papp_svc->file_close(fp);
    __builtin_memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    st->st_size = size;
    return 0;
}
int _stat_r(struct _reent *r, const char *path, struct stat *st) { return _stat(path, st); }
int stat(const char *path, struct stat *st) { return _stat(path, st); }

int _unlink(const char *path) { (void)path; return -1; }
int _unlink_r(struct _reent *r, const char *path) { return _unlink(path); }
int unlink(const char *path) { return _unlink(path); }
int _rename(const char *o, const char *n) { (void)o; (void)n; return -1; }
int rename(const char *o, const char *n) { return _rename(o, n); }
