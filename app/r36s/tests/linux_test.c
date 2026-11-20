#define _GNU_SOURCE
#include "xc_port_linux.h"
#include "xc_port.h"
#include "https.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

int main(void) {
    char root[] = "/tmp/orbita-linux-test-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    CHECK(orb_linux_init(root) == 0);
    struct stat st;
    CHECK(stat(root, &st) == 0 && (st.st_mode & 0777) == 0700);
    const char *token = "fake-refresh-token-for-offline-test";
    CHECK(xc_store_save("TOKEN.TXT", token, strlen(token)) == 0);
    char buf[128] = {0}, path[512];
    CHECK(xc_store_load("TOKEN.TXT", buf, sizeof(buf)) == (int)strlen(token));
    CHECK(!strcmp(buf, token));
    snprintf(path, sizeof(path), "%s/TOKEN.TXT", root);
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(xc_store_save("TOKEN.TXT", "rotated", 7) == 0);
    CHECK(xc_store_load("TOKEN.TXT", buf, sizeof(buf)) == 7 && !memcmp(buf, "rotated", 7));
    CHECK(xc_store_save("cache/names/test.txt", "names", 5) == 0);
    CHECK(xc_store_load("cache/names/test.txt", buf, sizeof(buf)) == 5);
    CHECK(xc_store_save("../outside", "x", 1) == -1);
    CHECK(xc_store_save("cache/../../outside", "x", 1) == -1);
    CHECK(xc_store_load("/etc/passwd", buf, sizeof(buf)) == -1);
    CHECK(xc_store_load("cache/../TOKEN.TXT", buf, sizeof(buf)) == -1);
    CHECK(xc_store_remove("../outside") == -1);
    snprintf(path, sizeof(path), "%s/link", root);
    CHECK(symlink("/tmp", path) == 0);
    CHECK(xc_store_save("link/orbita-escape", "x", 1) == -1);
    CHECK(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/secret-link", root);
    CHECK(symlink("/etc/passwd", path) == 0);
    CHECK(xc_store_load("secret-link", buf, sizeof(buf)) == -1);
    CHECK(xc_store_save("secret-link", "local", 5) == 0);
    CHECK(lstat(path, &st) == 0 && S_ISREG(st.st_mode));
    CHECK(xc_store_remove("secret-link") == 0);
    snprintf(path, sizeof(path), "%s/pipe", root);
    CHECK(mkfifo(path, 0600) == 0);
    CHECK(xc_store_load("pipe", buf, sizeof(buf)) == -1);
    CHECK(unlink(path) == 0);
    CHECK(xc_store_save("empty", "", 0) == 0);
    CHECK(xc_store_load("empty", buf, sizeof(buf)) == 0);
    unsigned char a[32], b[32];
    CHECK(https_random(a, sizeof(a)) == 0 && https_random(b, sizeof(b)) == 0);
    CHECK(memcmp(a, b, sizeof(a)) != 0);
    uint64_t before = xcn_now_ms();
    xcn_sleep_ms(10);
    CHECK(xcn_now_ms() >= before + 5);
    /* Sample in a defined order: comparing two calls in one expression can
     * fail across a millisecond boundary, especially under ARM emulation. */
    uint64_t clock_before = xcn_now_us();
    uint64_t clock_ms = xcn_now_ms();
    uint64_t clock_after = xcn_now_us();
    CHECK(clock_before / 1000 <= clock_ms && clock_ms <= clock_after / 1000);
    orb_linux_cancel(1); CHECK(xcn_cancelled());
    orb_linux_cancel(0); CHECK(!xcn_cancelled());
    CHECK(xc_time_now() > 1700000000LL);
    CHECK(xc_store_remove("TOKEN.TXT") == 0);
    CHECK(xc_store_load("TOKEN.TXT", buf, sizeof(buf)) == -1);
    CHECK(xc_store_remove("cache/names/test.txt") == 0);
    CHECK(xc_store_remove("empty") == 0);
    snprintf(path, sizeof(path), "%s/cache/names", root); CHECK(rmdir(path) == 0);
    snprintf(path, sizeof(path), "%s/cache", root); CHECK(rmdir(path) == 0);
    orb_linux_shutdown();
    CHECK(rmdir(root) == 0);
    printf("Linux platform: %d failures\n", failures);
    return failures ? 1 : 0;
}
