// test_edu.c — Userspace test program for /dev/edu0
//
// Cross-compile:
//   aarch64-linux-gnu-gcc -static -o test_edu test_edu.c
//
// Run inside QEMU guest:
//   /bin/test_edu

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

int main(void)
{
    int fd;
    char buf[64];
    int n;

    printf("=== EDU Device Test ===\n\n");

    fd = open("/dev/edu0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/edu0");
        return 1;
    }

    // Test 1: Read live clock register
    printf("[1] Reading live clock register...\n");
    memset(buf, 0, sizeof(buf));
    n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("    %s", buf);
    } else {
        perror("    read failed");
    }

    // Test 2: Compute factorials
    printf("\n[2] Computing factorials (check dmesg for results)...\n");
    const char *tests[] = { "1", "5", "10", "12", NULL };
    for (int i = 0; tests[i]; i++) {
        printf("    Writing %s! to device...\n", tests[i]);
        lseek(fd, 0, SEEK_SET);
        write(fd, tests[i], strlen(tests[i]));
        usleep(100000);  // 100ms — wait for IRQ and result
    }

    // Test 3: Read clock again to show it advanced
    printf("\n[3] Reading live clock again (should have advanced)...\n");
    lseek(fd, 0, SEEK_SET);
    memset(buf, 0, sizeof(buf));
    n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("    %s", buf);
    }

    close(fd);
    printf("\n=== Test complete. Check dmesg for IRQ and result logs. ===\n");
    return 0;
}
