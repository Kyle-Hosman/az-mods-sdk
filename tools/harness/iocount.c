/* SPDX-License-Identifier: GPL-3.0-only
 *
 * iocount.so: an LD_PRELOAD library for the test harness. While armed, it counts every
 * call into the C library that touches files, processes or the network, and every heap
 * allocation, then passes the call on unchanged. The harness arms it only around calls
 * into the mod under test.
 *
 * The harness finds azt_arm() and azt_counts() with dlsym(RTLD_DEFAULT, ...); when this
 * library is not preloaded they are missing and the harness reports those checks as not
 * measured (the sanitizer run, which brings its own allocator).
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))

/* glibc's fortified and versioned entry points, not all declared by the headers */
int __open_2(const char *path, int flags);
int __open64_2(const char *path, int flags);
int __openat_2(int dir, const char *path, int flags);
/* Before glibc 2.33, stat and lstat are inline wrappers around these; from 2.33 on they are
 * functions of their own (wrapped below) and these are no longer declared. */
int __xstat(int ver, const char *path, struct stat *st);
int __lxstat(int ver, const char *path, struct stat *st);
int __xstat64(int ver, const char *path, struct stat64 *st);

#define HAVE_STAT_FUNCTIONS 0
#ifdef __GLIBC_PREREQ
#if __GLIBC_PREREQ(2, 33)
#undef HAVE_STAT_FUNCTIONS
#define HAVE_STAT_FUNCTIONS 1
#endif
#endif

static __thread int armed;   /* per thread: a mod's own threads may read and allocate */
static unsigned long io_calls, alloc_calls;
static char first_io[64], first_alloc[64];

EXPORT void azt_arm(int on) { armed = on; }

EXPORT void azt_counts(unsigned long *io, unsigned long *alloc, const char **io_name,
                       const char **alloc_name)
{
    *io = io_calls;
    *alloc = alloc_calls;
    *io_name = first_io;
    *alloc_name = first_alloc;
}

EXPORT void azt_reset(void)
{
    io_calls = alloc_calls = 0;
    first_io[0] = first_alloc[0] = '\0';
}

static void note_io(const char *name)
{
    if (!armed) return;
    if (__atomic_fetch_add(&io_calls, 1, __ATOMIC_RELAXED) == 0) snprintf(first_io, sizeof first_io, "%s", name);
}

static void note_alloc(const char *name)
{
    if (!armed) return;
    if (__atomic_fetch_add(&alloc_calls, 1, __ATOMIC_RELAXED) == 0) snprintf(first_alloc, sizeof first_alloc, "%s", name);
}

/* Each wrapped function's next definition, looked up once at load: dlsym may allocate, and
 * an allocation made while armed would be blamed on the mod. */
#define NEXT(name) ((void)0)

static __typeof__(open) *next_open;
static __typeof__(open64) *next_open64;
static __typeof__(openat) *next_openat;
static __typeof__(openat64) *next_openat64;
static __typeof__(__open_2) *next___open_2;
static __typeof__(__open64_2) *next___open64_2;
static __typeof__(__openat_2) *next___openat_2;
static __typeof__(creat) *next_creat;
static __typeof__(fopen) *next_fopen;
static __typeof__(fopen64) *next_fopen64;
static __typeof__(freopen) *next_freopen;
static __typeof__(opendir) *next_opendir;
static __typeof__(read) *next_read;
static __typeof__(write) *next_write;
static __typeof__(pread) *next_pread;
static __typeof__(pwrite) *next_pwrite;
static __typeof__(fread) *next_fread;
static __typeof__(fwrite) *next_fwrite;
static __typeof__(access) *next_access;
static __typeof__(__xstat) *next___xstat;
static __typeof__(__lxstat) *next___lxstat;
static __typeof__(__xstat64) *next___xstat64;
#if HAVE_STAT_FUNCTIONS
static __typeof__(stat) *next_stat;
static __typeof__(lstat) *next_lstat;
static __typeof__(stat64) *next_stat64;
#endif
static __typeof__(mkdir) *next_mkdir;
static __typeof__(unlink) *next_unlink;
static __typeof__(rename) *next_rename;
static __typeof__(remove) *next_remove;
static __typeof__(socket) *next_socket;
static __typeof__(connect) *next_connect;
static __typeof__(fork) *next_fork;
static __typeof__(system) *next_system;
static __typeof__(popen) *next_popen;
static __typeof__(execve) *next_execve;
static __typeof__(usleep) *next_usleep;
static __typeof__(sleep) *next_sleep;

/* ---- files ---------------------------------------------------------------------------- */

EXPORT int open(const char *path, int flags, ...)
{
    va_list ap;
    va_start(ap, flags);
    mode_t mode = va_arg(ap, mode_t);
    va_end(ap);
    note_io("open");
    NEXT(open);
    return next_open(path, flags, mode);
}
EXPORT int open64(const char *path, int flags, ...)
{
    va_list ap;
    va_start(ap, flags);
    mode_t mode = va_arg(ap, mode_t);
    va_end(ap);
    note_io("open64");
    NEXT(open64);
    return next_open64(path, flags, mode);
}
EXPORT int openat(int dir, const char *path, int flags, ...)
{
    va_list ap;
    va_start(ap, flags);
    mode_t mode = va_arg(ap, mode_t);
    va_end(ap);
    note_io("openat");
    NEXT(openat);
    return next_openat(dir, path, flags, mode);
}
EXPORT int openat64(int dir, const char *path, int flags, ...)
{
    va_list ap;
    va_start(ap, flags);
    mode_t mode = va_arg(ap, mode_t);
    va_end(ap);
    note_io("openat64");
    NEXT(openat64);
    return next_openat64(dir, path, flags, mode);
}
EXPORT int __open_2(const char *path, int flags)
{
    note_io("__open_2");
    NEXT(__open_2);
    return next___open_2(path, flags);
}
EXPORT int __open64_2(const char *path, int flags)
{
    note_io("__open64_2");
    NEXT(__open64_2);
    return next___open64_2(path, flags);
}
EXPORT int __openat_2(int dir, const char *path, int flags)
{
    note_io("__openat_2");
    NEXT(__openat_2);
    return next___openat_2(dir, path, flags);
}
EXPORT int creat(const char *path, mode_t mode)
{
    note_io("creat");
    NEXT(creat);
    return next_creat(path, mode);
}
EXPORT FILE *fopen(const char *path, const char *mode)
{
    note_io("fopen");
    NEXT(fopen);
    return next_fopen(path, mode);
}
EXPORT FILE *fopen64(const char *path, const char *mode)
{
    note_io("fopen64");
    NEXT(fopen64);
    return next_fopen64(path, mode);
}
EXPORT FILE *freopen(const char *path, const char *mode, FILE *f)
{
    note_io("freopen");
    NEXT(freopen);
    return next_freopen(path, mode, f);
}
EXPORT DIR *opendir(const char *path)
{
    note_io("opendir");
    NEXT(opendir);
    return next_opendir(path);
}
EXPORT ssize_t read(int fd, void *buf, size_t n)
{
    note_io("read");
    NEXT(read);
    return next_read(fd, buf, n);
}
EXPORT ssize_t write(int fd, const void *buf, size_t n)
{
    note_io("write");
    NEXT(write);
    return next_write(fd, buf, n);
}
EXPORT ssize_t pread(int fd, void *buf, size_t n, off_t off)
{
    note_io("pread");
    NEXT(pread);
    return next_pread(fd, buf, n, off);
}
EXPORT ssize_t pwrite(int fd, const void *buf, size_t n, off_t off)
{
    note_io("pwrite");
    NEXT(pwrite);
    return next_pwrite(fd, buf, n, off);
}
EXPORT size_t fread(void *p, size_t s, size_t n, FILE *f)
{
    note_io("fread");
    NEXT(fread);
    return next_fread(p, s, n, f);
}
EXPORT size_t fwrite(const void *p, size_t s, size_t n, FILE *f)
{
    note_io("fwrite");
    NEXT(fwrite);
    return next_fwrite(p, s, n, f);
}
EXPORT int fprintf(FILE *f, const char *fmt, ...)
{
    note_io("fprintf");
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}
EXPORT int access(const char *path, int mode)
{
    note_io("access");
    NEXT(access);
    return next_access(path, mode);
}
EXPORT int __xstat(int ver, const char *path, struct stat *st)
{
    note_io("stat");
    NEXT(__xstat);
    return next___xstat(ver, path, st);
}
EXPORT int __lxstat(int ver, const char *path, struct stat *st)
{
    note_io("lstat");
    NEXT(__lxstat);
    return next___lxstat(ver, path, st);
}
EXPORT int __xstat64(int ver, const char *path, struct stat64 *st)
{
    note_io("stat64");
    NEXT(__xstat64);
    return next___xstat64(ver, path, st);
}
#if HAVE_STAT_FUNCTIONS
EXPORT int stat(const char *path, struct stat *st)
{
    note_io("stat");
    NEXT(stat);
    return next_stat(path, st);
}
EXPORT int lstat(const char *path, struct stat *st)
{
    note_io("lstat");
    NEXT(lstat);
    return next_lstat(path, st);
}
EXPORT int stat64(const char *path, struct stat64 *st)
{
    note_io("stat64");
    NEXT(stat64);
    return next_stat64(path, st);
}
#endif
EXPORT int mkdir(const char *path, mode_t mode)
{
    note_io("mkdir");
    NEXT(mkdir);
    return next_mkdir(path, mode);
}
EXPORT int unlink(const char *path)
{
    note_io("unlink");
    NEXT(unlink);
    return next_unlink(path);
}
EXPORT int rename(const char *from, const char *to)
{
    note_io("rename");
    NEXT(rename);
    return next_rename(from, to);
}
EXPORT int remove(const char *path)
{
    note_io("remove");
    NEXT(remove);
    return next_remove(path);
}

/* ---- network and processes -------------------------------------------------------------- */

EXPORT int socket(int domain, int type, int proto)
{
    note_io("socket");
    NEXT(socket);
    return next_socket(domain, type, proto);
}
EXPORT int connect(int fd, const struct sockaddr *a, socklen_t n)
{
    note_io("connect");
    NEXT(connect);
    return next_connect(fd, a, n);
}
EXPORT pid_t fork(void)
{
    note_io("fork");
    NEXT(fork);
    return next_fork();
}
EXPORT int system(const char *cmd)
{
    note_io("system");
    NEXT(system);
    return next_system(cmd);
}
EXPORT FILE *popen(const char *cmd, const char *mode)
{
    note_io("popen");
    NEXT(popen);
    return next_popen(cmd, mode);
}
EXPORT int execve(const char *path, char *const argv[], char *const envp[])
{
    note_io("execve");
    NEXT(execve);
    return next_execve(path, argv, envp);
}
EXPORT int usleep(useconds_t us)
{
    note_io("usleep");
    NEXT(usleep);
    return next_usleep(us);
}
EXPORT unsigned sleep(unsigned s)
{
    note_io("sleep");
    NEXT(sleep);
    return next_sleep(s);
}

__attribute__((constructor)) static void resolve_next(void)
{
    next_open = (__typeof__(open) *)dlsym(RTLD_NEXT, "open");
    next_open64 = (__typeof__(open64) *)dlsym(RTLD_NEXT, "open64");
    next_openat = (__typeof__(openat) *)dlsym(RTLD_NEXT, "openat");
    next_openat64 = (__typeof__(openat64) *)dlsym(RTLD_NEXT, "openat64");
    next___open_2 = (__typeof__(__open_2) *)dlsym(RTLD_NEXT, "__open_2");
    next___open64_2 = (__typeof__(__open64_2) *)dlsym(RTLD_NEXT, "__open64_2");
    next___openat_2 = (__typeof__(__openat_2) *)dlsym(RTLD_NEXT, "__openat_2");
    next_creat = (__typeof__(creat) *)dlsym(RTLD_NEXT, "creat");
    next_fopen = (__typeof__(fopen) *)dlsym(RTLD_NEXT, "fopen");
    next_fopen64 = (__typeof__(fopen64) *)dlsym(RTLD_NEXT, "fopen64");
    next_freopen = (__typeof__(freopen) *)dlsym(RTLD_NEXT, "freopen");
    next_opendir = (__typeof__(opendir) *)dlsym(RTLD_NEXT, "opendir");
    next_read = (__typeof__(read) *)dlsym(RTLD_NEXT, "read");
    next_write = (__typeof__(write) *)dlsym(RTLD_NEXT, "write");
    next_pread = (__typeof__(pread) *)dlsym(RTLD_NEXT, "pread");
    next_pwrite = (__typeof__(pwrite) *)dlsym(RTLD_NEXT, "pwrite");
    next_fread = (__typeof__(fread) *)dlsym(RTLD_NEXT, "fread");
    next_fwrite = (__typeof__(fwrite) *)dlsym(RTLD_NEXT, "fwrite");
    next_access = (__typeof__(access) *)dlsym(RTLD_NEXT, "access");
    next___xstat = (__typeof__(__xstat) *)dlsym(RTLD_NEXT, "__xstat");
    next___lxstat = (__typeof__(__lxstat) *)dlsym(RTLD_NEXT, "__lxstat");
    next___xstat64 = (__typeof__(__xstat64) *)dlsym(RTLD_NEXT, "__xstat64");
#if HAVE_STAT_FUNCTIONS
    next_stat = (__typeof__(stat) *)dlsym(RTLD_NEXT, "stat");
    next_lstat = (__typeof__(lstat) *)dlsym(RTLD_NEXT, "lstat");
    next_stat64 = (__typeof__(stat64) *)dlsym(RTLD_NEXT, "stat64");
#endif
    next_mkdir = (__typeof__(mkdir) *)dlsym(RTLD_NEXT, "mkdir");
    next_unlink = (__typeof__(unlink) *)dlsym(RTLD_NEXT, "unlink");
    next_rename = (__typeof__(rename) *)dlsym(RTLD_NEXT, "rename");
    next_remove = (__typeof__(remove) *)dlsym(RTLD_NEXT, "remove");
    next_socket = (__typeof__(socket) *)dlsym(RTLD_NEXT, "socket");
    next_connect = (__typeof__(connect) *)dlsym(RTLD_NEXT, "connect");
    next_fork = (__typeof__(fork) *)dlsym(RTLD_NEXT, "fork");
    next_system = (__typeof__(system) *)dlsym(RTLD_NEXT, "system");
    next_popen = (__typeof__(popen) *)dlsym(RTLD_NEXT, "popen");
    next_execve = (__typeof__(execve) *)dlsym(RTLD_NEXT, "execve");
    next_usleep = (__typeof__(usleep) *)dlsym(RTLD_NEXT, "usleep");
    next_sleep = (__typeof__(sleep) *)dlsym(RTLD_NEXT, "sleep");
}

/* ---- heap ------------------------------------------------------------------------------- */

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void *__libc_memalign(size_t, size_t);

EXPORT void *malloc(size_t n)
{
    note_alloc("malloc");
    return __libc_malloc(n);
}
EXPORT void *calloc(size_t a, size_t b)
{
    note_alloc("calloc");
    return __libc_calloc(a, b);
}
EXPORT void *realloc(void *p, size_t n)
{
    note_alloc("realloc");
    return __libc_realloc(p, n);
}
EXPORT void *memalign(size_t a, size_t n)
{
    note_alloc("memalign");
    return __libc_memalign(a, n);
}
EXPORT int posix_memalign(void **out, size_t a, size_t n)
{
    note_alloc("posix_memalign");
    void *p = __libc_memalign(a, n);
    if (!p) return 12;
    *out = p;
    return 0;
}
EXPORT void *aligned_alloc(size_t a, size_t n)
{
    note_alloc("aligned_alloc");
    return __libc_memalign(a, n);
}
