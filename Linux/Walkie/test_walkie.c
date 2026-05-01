/*
 * test_walkie.c  –  test suite for the walkie two-process IPC character device
 *
 * Build:  gcc -Wall -Wextra -o test_walkie test_walkie.c
 * Run:    sudo ./test_walkie          (needs r/w access to /dev/walkie)
 *
 * Tests
 * -----
 *  1. Basic write / read round-trip
 *  2. Cross-side isolation  (side-0 cannot read its own writes)
 *  3. Third open is rejected with EBUSY
 *  4. Non-blocking read on empty buffer returns EAGAIN
 *  5. Non-blocking write on full buffer returns EAGAIN
 *  6. EOF detected after peer closes
 *  7. poll()/select() reports EPOLLIN when data is available
 *  8. Large message (> BUF_SIZE / 2)
 *  9. Interleaved bidirectional messaging
 * 10. Reopen after both sides close
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/types.h>

#define DEVICE   "/dev/walkie"
#define BUF_SIZE 4096

/* ── helpers ─────────────────────────────────────────────────────────────── */

static int passed = 0;
static int failed = 0;

#define PASS(name)  do { printf("  [PASS] %s\n", (name)); passed++; } while(0)
#define FAIL(name, fmt, ...) \
    do { printf("  [FAIL] %s: " fmt "\n", (name), ##__VA_ARGS__); failed++; } while(0)

/* Open the device, fail hard on error. */
static int xopen(int flags)
{
    int fd = open(DEVICE, flags);
    if (fd < 0) {
        perror("open " DEVICE);
        exit(EXIT_FAILURE);
    }
    return fd;
}

/* ── individual tests ────────────────────────────────────────────────────── */

/* T1: basic write → read */
static void test_basic_rw(void)
{
    const char *name = "T1 basic write/read";
    const char *msg  = "hello walkie";
    char buf[64]     = {0};
    int fd0, fd1;
    ssize_t n;

    fd0 = xopen(O_RDWR);
    fd1 = xopen(O_RDWR);

    write(fd0, msg, strlen(msg));
    n = read(fd1, buf, sizeof(buf) - 1);

    if (n == (ssize_t)strlen(msg) && memcmp(buf, msg, n) == 0)
        PASS(name);
    else
        FAIL(name, "got %zd bytes: '%s'", n, buf);

    close(fd0);
    close(fd1);
}

/* T2: side-0 cannot read its own write; only side-1 can */
static void test_cross_side_isolation(void)
{
    const char *name = "T2 cross-side isolation";
    const char *msg  = "from side 0";
    char buf[64]     = {0};
    int fd0, fd1;
    ssize_t n;

    fd0 = xopen(O_RDWR | O_NONBLOCK);
    fd1 = xopen(O_RDWR | O_NONBLOCK);

    write(fd0, msg, strlen(msg));

    /* side-0 should NOT see its own data */
    n = read(fd0, buf, sizeof(buf));
    if (n != -1 || errno != EAGAIN) {
        FAIL(name, "side-0 read its own write (n=%zd)", n);
        close(fd0); close(fd1);
        return;
    }

    /* side-1 SHOULD see it */
    n = read(fd1, buf, sizeof(buf));
    if (n == (ssize_t)strlen(msg) && memcmp(buf, msg, n) == 0)
        PASS(name);
    else
        FAIL(name, "side-1 got %zd bytes", n);

    close(fd0);
    close(fd1);
}

/* T3: third open must fail with EBUSY */
static void test_third_open_rejected(void)
{
    const char *name = "T3 third open rejected";
    int fd0, fd1, fd2;

    fd0 = xopen(O_RDWR);
    fd1 = xopen(O_RDWR);
    fd2 = open(DEVICE, O_RDWR);

    if (fd2 == -1 && errno == EBUSY)
        PASS(name);
    else
        FAIL(name, "expected EBUSY, got fd=%d errno=%d", fd2, errno);

    if (fd2 >= 0) close(fd2);
    close(fd0);
    close(fd1);
}

/* T4: non-blocking read on empty buffer → EAGAIN */
static void test_nonblock_read_empty(void)
{
    const char *name = "T4 non-blocking read empty";
    char buf[64];
    int fd0, fd1;
    ssize_t n;

    fd0 = xopen(O_RDWR | O_NONBLOCK);
    fd1 = xopen(O_RDWR | O_NONBLOCK);

    /* Nothing written by fd0, so fd1 has nothing to read */
    n = read(fd1, buf, sizeof(buf));

    if (n == -1 && errno == EAGAIN)
        PASS(name);
    else
        FAIL(name, "expected EAGAIN, got n=%zd errno=%d", n, errno);

    close(fd0);
    close(fd1);
}

/* T5: non-blocking write on full buffer → EAGAIN */
static void test_nonblock_write_full(void)
{
    const char *name = "T5 non-blocking write full";
    char wbuf[BUF_SIZE];
    int fd0, fd1;
    ssize_t n;

    memset(wbuf, 'X', sizeof(wbuf));

    fd0 = xopen(O_RDWR | O_NONBLOCK);
    fd1 = xopen(O_RDWR | O_NONBLOCK);

    /* Fill side-0's pipe completely */
    write(fd0, wbuf, sizeof(wbuf));

    /* Next write should be rejected */
    n = write(fd0, "extra", 5);

    if (n == -1 && errno == EAGAIN)
        PASS(name);
    else
        FAIL(name, "expected EAGAIN, got n=%zd errno=%d", n, errno);

    close(fd0);
    close(fd1);
}

/* T6: EOF when peer closes */
static void test_eof_on_peer_close(void)
{
    const char *name = "T6 EOF after peer close";
    char buf[64];
    int fd0, fd1;
    ssize_t n;

    fd0 = xopen(O_RDWR);
    fd1 = xopen(O_RDWR);

    close(fd0);   /* peer (side-0) closes */

    /* side-1 should see EOF (0 bytes) on an empty pipe with no peer */
    n = read(fd1, buf, sizeof(buf));

    if (n == 0)
        PASS(name);
    else
        FAIL(name, "expected 0 (EOF), got n=%zd errno=%d", n, errno);

    close(fd1);
}

/* T7: poll reports EPOLLIN when data is available */
static void test_poll_readable(void)
{
    const char *name = "T7 poll EPOLLIN";
    const char *msg  = "poll test";
    struct pollfd pfd;
    int fd0, fd1, ret;

    fd0 = xopen(O_RDWR);
    fd1 = xopen(O_RDWR);

    write(fd0, msg, strlen(msg));

    pfd.fd     = fd1;
    pfd.events = POLLIN;
    ret = poll(&pfd, 1, 500 /* ms */);

    if (ret == 1 && (pfd.revents & POLLIN))
        PASS(name);
    else
        FAIL(name, "poll returned %d revents=0x%x", ret, pfd.revents);

    close(fd0);
    close(fd1);
}

/* T8: large message (half BUF_SIZE) survives intact */
static void test_large_message(void)
{
    const char *name = "T8 large message";
    char wbuf[BUF_SIZE / 2];
    char rbuf[BUF_SIZE / 2];
    int fd0, fd1;
    ssize_t n;

    memset(wbuf, 'A', sizeof(wbuf));

    fd0 = xopen(O_RDWR);
    fd1 = xopen(O_RDWR);

    write(fd0, wbuf, sizeof(wbuf));
    n = read(fd1, rbuf, sizeof(rbuf));

    if (n == (ssize_t)sizeof(wbuf) && memcmp(wbuf, rbuf, n) == 0)
        PASS(name);
    else
        FAIL(name, "got %zd bytes", n);

    close(fd0);
    close(fd1);
}

/* T9: bidirectional exchange via fork */
static void test_bidirectional(void)
{
    const char *name = "T9 bidirectional";
    const char *ping = "ping";
    const char *pong = "pong";
    char buf[16]     = {0};
    int fd0, fd1;
    pid_t child;
    int status;

    fd0 = xopen(O_RDWR);
    fd1 = xopen(O_RDWR);

    child = fork();
    if (child == 0) {
        /* child: side-1 — wait for ping, send pong */
        close(fd0);
        memset(buf, 0, sizeof(buf));
        read(fd1, buf, sizeof(buf) - 1);
        write(fd1, pong, strlen(pong));
        close(fd1);
        exit(0);
    }

    /* parent: side-0 — send ping, wait for pong */
    close(fd1);
    write(fd0, ping, strlen(ping));
    memset(buf, 0, sizeof(buf));
    read(fd0, buf, sizeof(buf) - 1);
    close(fd0);
    waitpid(child, &status, 0);

    if (strcmp(buf, pong) == 0)
        PASS(name);
    else
        FAIL(name, "expected 'pong', got '%s'", buf);
}

/* T10: reopen after both sides close */
static void test_reopen(void)
{
    const char *name = "T10 reopen after close";
    const char *msg  = "reopen ok";
    char buf[64]     = {0};
    int fd0, fd1;
    ssize_t n;

    /* first session */
    fd0 = xopen(O_RDWR);
    fd1 = xopen(O_RDWR);
    close(fd0);
    close(fd1);

    /* second session */
    fd0 = open(DEVICE, O_RDWR);
    fd1 = open(DEVICE, O_RDWR);

    if (fd0 < 0 || fd1 < 0) {
        FAIL(name, "reopen failed (errno=%d)", errno);
        if (fd0 >= 0) close(fd0);
        if (fd1 >= 0) close(fd1);
        return;
    }

    write(fd0, msg, strlen(msg));
    n = read(fd1, buf, sizeof(buf) - 1);

    if (n == (ssize_t)strlen(msg) && memcmp(buf, msg, n) == 0)
        PASS(name);
    else
        FAIL(name, "got %zd bytes: '%s'", n, buf);

    close(fd0);
    close(fd1);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== walkie device test suite ===\n\n");

    test_basic_rw();
    test_cross_side_isolation();
    test_third_open_rejected();
    test_nonblock_read_empty();
    test_nonblock_write_full();
    test_eof_on_peer_close();
    test_poll_readable();
    test_large_message();
    test_bidirectional();
    test_reopen();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
