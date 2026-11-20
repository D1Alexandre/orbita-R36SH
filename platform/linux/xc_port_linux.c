#define _GNU_SOURCE
#include "xc_port_linux.h"
#include "xc_port.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int store_fd = -1;
static char store_path[PATH_MAX];
static atomic_int cancelled;
static atomic_uint temp_seq;
static int (*cancel_hook)(void);
static pthread_mutex_t time_mu = PTHREAD_MUTEX_INITIALIZER;
static long long time_offset;

static int mkdir_tree(const char *path) {
    char p[PATH_MAX];
    if (!path || !*path || strlen(path) >= sizeof(p)) return -1;
    strcpy(p, path);
    for (char *q = p + 1; *q; ++q) {
        if (*q != '/') continue;
        *q = 0;
        if (mkdir(p, 0700) && errno != EEXIST) return -1;
        *q = '/';
    }
    return mkdir(p, 0700) && errno != EEXIST ? -1 : 0;
}

int orb_linux_init(const char *data_dir) {
    const char *base;
    int n;
    if (store_fd >= 0) return -1;
    if (data_dir && *data_dir) n = snprintf(store_path, sizeof(store_path), "%s", data_dir);
    else if ((base = getenv("XDG_DATA_HOME")) && *base)
        n = snprintf(store_path, sizeof(store_path), "%s/orbita", base);
    else if ((base = getenv("HOME")) && *base)
        n = snprintf(store_path, sizeof(store_path), "%s/.local/share/orbita", base);
    else return -1;
    if (n < 0 || n >= (int)sizeof(store_path) || mkdir_tree(store_path)) return -1;
    store_fd = open(store_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (store_fd < 0) return -1;
    if (fchmod(store_fd, 0700)) { close(store_fd); store_fd = -1; return -1; }
    atomic_store(&cancelled, 0);
    return 0;
}

void orb_linux_shutdown(void) {
    if (store_fd >= 0) close(store_fd);
    store_fd = -1;
}
const char *orb_linux_data_dir(void) { return store_path; }
void orb_linux_cancel(int cancel) { atomic_store(&cancelled, !!cancel); }
int xcn_cancelled(void) { return atomic_load(&cancelled) || (cancel_hook && cancel_hook()); }
void xcn_set_cancel_hook(int (*fn)(void)) { cancel_hook = fn; }

uint64_t xcn_now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + (uint64_t)t.tv_nsec / 1000;
}
uint64_t xcn_now_ms(void) { return xcn_now_us() / 1000; }
void xcn_sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) && errno == EINTR) {}
}
long long xc_time_now(void) {
    pthread_mutex_lock(&time_mu);
    long long t = (long long)time(NULL) + time_offset;
    pthread_mutex_unlock(&time_mu);
    return t;
}
long long xc_time_floor(void) { return 0; }
void xc_time_set(long long t, const char *source) {
    (void)source;
    pthread_mutex_lock(&time_mu);
    time_offset = t - (long long)time(NULL);
    pthread_mutex_unlock(&time_mu);
}

void xc_log(const char *fmt, ...) {
    /* Device codes belong on the display, never in persistent diagnostics. */
    if (!strncmp(fmt, "[login] codigo ", 15)) return;
    flockfile(stderr);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    funlockfile(stderr);
}

/* Traverse each relative component without following symlinks. */
static int parent_fd(const char *name, char leaf[NAME_MAX + 1], int create) {
    if (store_fd < 0 || !name || !*name || *name == '/' || strlen(name) >= PATH_MAX) return -1;
    char path[PATH_MAX];
    strcpy(path, name);
    int fd = dup(store_fd);
    if (fd < 0) return -1;
    char *part = path;
    for (;;) {
        char *slash = strchr(part, '/');
        if (slash) *slash = 0;
        if (!*part || !strcmp(part, ".") || !strcmp(part, "..") || strlen(part) > NAME_MAX) break;
        if (!slash) { strcpy(leaf, part); return fd; }
        if (create && mkdirat(fd, part, 0700) && errno != EEXIST) break;
        int next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) break;
        close(fd);
        fd = next;
        part = slash + 1;
    }
    close(fd);
    return -1;
}

int xc_store_load(const char *name, void *buf, size_t cap) {
    char leaf[NAME_MAX + 1];
    int dir = parent_fd(name, leaf, 0);
    if (dir < 0) return -1;
    int fd = openat(dir, leaf, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    close(dir);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); return -1; }
    size_t total = 0;
    int failed = 0;
    while (total < cap) {
        ssize_t n = read(fd, (char *)buf + total, cap - total);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) failed = 1;
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(fd);
    return failed || total > INT_MAX ? -1 : (int)total;
}

int xc_store_save(const char *name, const void *buf, size_t n) {
    char leaf[NAME_MAX + 1], temp[80];
    int dir = parent_fd(name, leaf, 1);
    if (dir < 0) return -1;
    snprintf(temp, sizeof(temp), ".orbita-%ld-%u.tmp", (long)getpid(), atomic_fetch_add(&temp_seq, 1));
    int fd = openat(dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) { close(dir); return -1; }
    size_t total = 0;
    int rc = -1;
    while (total < n) {
        ssize_t w = write(fd, (const char *)buf + total, n - total);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) goto done;
        total += (size_t)w;
    }
    if (fsync(fd)) goto done;
    if (close(fd)) { fd = -1; goto done; }
    fd = -1;
    if (renameat(dir, temp, dir, leaf)) goto done;
    rc = fsync(dir);
done:
    if (fd >= 0) close(fd);
    if (rc) unlinkat(dir, temp, 0);
    close(dir);
    return rc;
}

int xc_store_remove(const char *name) {
    char leaf[NAME_MAX + 1];
    int dir = parent_fd(name, leaf, 0);
    if (dir < 0) return -1;
    int rc = unlinkat(dir, leaf, 0);
    if (!rc) rc = fsync(dir);
    close(dir);
    return rc;
}

size_t xc_seed_material(uint8_t *out, size_t cap) {
    size_t n = 0;
    while (n < cap) {
        ssize_t r = getrandom(out + n, cap - n, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        n += (size_t)r;
    }
    return n;
}
