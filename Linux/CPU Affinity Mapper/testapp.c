#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/syscall.h>

#define DEVICE_PATH "/dev/cpu_affinity_mapper"

/* IOCTL commands - must match driver */
#define CAM_MAGIC 'C'
#define CAM_SET_AFFINITY    _IOW(CAM_MAGIC, 1, struct affinity_request)
#define CAM_GET_AFFINITY    _IOR(CAM_MAGIC, 2, struct affinity_info)
#define CAM_CLEAR_MAPPING   _IOW(CAM_MAGIC, 3, pid_t)
#define CAM_GET_STATS       _IOR(CAM_MAGIC, 4, struct mapping_stats)

/* Data structures - must match driver */
struct affinity_request {
    pid_t pid;
    pid_t tid;
    unsigned long cpu_mask;
    int policy;
};

struct affinity_info {
    pid_t pid;
    pid_t tid;
    unsigned long current_mask;
    unsigned long requested_mask;
    int policy;
    int status;
};

struct mapping_stats {
    int total_mappings;
    int active_mappings;
    int failed_mappings;
    unsigned long total_switches;
};

/* Thread function for testing */
void *worker_thread(void *arg) {
    int thread_id = *(int *)arg;
    pid_t tid = syscall(SYS_gettid);
    
    printf("Thread %d started with TID: %d\n", thread_id, tid);
    
    /* Do some work */
    for (int i = 0; i < 1000000000; i++) {
        /* Busy work */
        asm volatile("nop");
    }
    
    printf("Thread %d (TID: %d) finished\n", thread_id, tid);
    return NULL;
}

int test_basic_functionality(void) {
    int fd;
    struct affinity_request req;
    struct affinity_info info;
    struct mapping_stats stats;
    pid_t pid = getpid();
    
    printf("\n=== Testing Basic Functionality ===\n");
    
    /* Open device */
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return -1;
    }
    
    /* Test 1: Set process affinity to CPU 0 */
    printf("Test 1: Setting process affinity to CPU 0\n");
    req.pid = pid;
    req.tid = 0;  /* 0 means process */
    req.cpu_mask = 0x1;  /* CPU 0 only */
    req.policy = 0;  /* Strict */
    
    if (ioctl(fd, CAM_SET_AFFINITY, &req) < 0) {
        perror("Failed to set affinity");
        close(fd);
        return -1;
    }
    printf("Process affinity set successfully\n");
    
    /* Test 2: Get affinity info */
    printf("Test 2: Getting affinity info\n");
    info.pid = pid;
    info.tid = 0;
    
    if (ioctl(fd, CAM_GET_AFFINITY, &info) < 0) {
        perror("Failed to get affinity");
        close(fd);
        return -1;
    }
    
    printf("Affinity info - PID: %d, Current mask: 0x%lx, Policy: %d, Status: %d\n",
           info.pid, info.current_mask, info.policy, info.status);
    
    /* Test 3: Get statistics */
    printf("Test 3: Getting statistics\n");
    if (ioctl(fd, CAM_GET_STATS, &stats) < 0) {
        perror("Failed to get stats");
        close(fd);
        return -1;
    }
    
    printf("Statistics - Total: %d, Active: %d, Failed: %d, Switches: %lu\n",
           stats.total_mappings, stats.active_mappings, 
           stats.failed_mappings, stats.total_switches);
    
    close(fd);
    return 0;
}

int test_thread_affinity(void) {
    int fd;
    pthread_t threads[4];
    int thread_ids[4];
    struct affinity_request req;
    pid_t pid = getpid();
    
    printf("\n=== Testing Thread Affinity ===\n");
    
    /* Open device */
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return -1;
    }
    
    /* Create threads */
    for (int i = 0; i < 4; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, worker_thread, &thread_ids[i]) != 0) {
            perror("Failed to create thread");
            close(fd);
            return -1;
        }
    }
    
    /* Wait a bit for threads to start */
    usleep(100000);
    
    /* Set affinity for each thread to different CPUs */
    for (int i = 0; i < 4; i++) {
        pid_t tid = syscall(SYS_gettid);  /* This won't work for other threads */
        
        printf("Setting thread %d affinity to CPU %d\n", i, i % 4);
        req.pid = pid;
        req.tid = 0;  /* We'd need actual TID here */
        req.cpu_mask = 1UL << (i % 4);
        req.policy = 0;
        
        if (ioctl(fd, CAM_SET_AFFINITY, &req) < 0) {
            printf("Warning: Failed to set affinity for thread %d\n", i);
        }
    }
    
    /* Wait for threads to complete */
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    close(fd);
    return 0;
}

int test_device_io(void) {
    int fd;
    char buffer[1024];
    ssize_t bytes_read;
    
    printf("\n=== Testing Device I/O ===\n");
    
    /* Test read operation */
    fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device for reading");
        return -1;
    }
    
    bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Device info:\n%s", buffer);
    } else {
        perror("Failed to read from device");
    }
    
    close(fd);
    
    /* Test write operation */
    fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device for writing");
        return -1;
    }
    
    char cmd[] = "1234:0:3";  /* PID:TID:MASK format */
    ssize_t bytes_written = write(fd, cmd, strlen(cmd));
    if (bytes_written > 0) {
        printf("Successfully wrote command: %s\n", cmd);
    } else {
        perror("Failed to write to device");
    }
    
    close(fd);
    return 0;
}

int test_proc_interface(void) {
    FILE *proc_file;
    char buffer[1024];
    
    printf("\n=== Testing Proc Interface ===\n");
    
    proc_file = fopen("/proc/cpu_affinity_mapper", "r");
    if (!proc_file) {
        perror("Failed to open proc file");
        return -1;
    }
    
    printf("Proc file contents:\n");
    while (fgets(buffer, sizeof(buffer), proc_file)) {
        printf("%s", buffer);
    }
    
    fclose(proc_file);
    return 0;
}

void print_cpu_info(void) {
    printf("\n=== System CPU Information ===\n");
    printf("Number of CPUs: %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    
    /* Show current process affinity */
    cpu_set_t mask;
    CPU_ZERO(&mask);
    
    if (sched_getaffinity(0, sizeof(mask), &mask) == 0) {
        printf("Current process affinity: ");
        for (int i = 0; i < CPU_SETSIZE; i++) {
            if (CPU_ISSET(i, &mask)) {
                printf("%d ", i);
            }
        }
        printf("\n");
    }
}

void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -b, --basic    Run basic functionality tests\n");
    printf("  -t, --threads  Run thread affinity tests\n");
    printf("  -i, --io       Run device I/O tests\n");
    printf("  -p, --proc     Test proc interface\n");
    printf("  -a, --all      Run all tests (default)\n");
}

int main(int argc, char *argv[]) {
    int run_basic = 0, run_threads = 0, run_io = 0, run_proc = 0;
    int run_all = 1;  /* Default to running all tests */
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--basic") == 0) {
            run_basic = 1;
            run_all = 0;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
            run_threads = 1;
            run_all = 0;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--io") == 0) {
            run_io = 1;
            run_all = 0;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--proc") == 0) {
            run_proc = 1;
            run_all = 0;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            run_all = 1;
        }
    }
    
    printf("CPU Affinity Mapper Test Application\n");
    printf("====================================\n");
    
    /* Check if we're running as root */
    if (geteuid() != 0) {
        printf("Warning: Not running as root. Some operations may fail.\n");
    }
    
    print_cpu_info();
    
    /* Run tests based on command line arguments */
    if (run_all || run_basic) {
        if (test_basic_functionality() < 0) {
            printf("Basic functionality test failed\n");
        }
    }
    
    if (run_all || run_threads) {
        if (test_thread_affinity() < 0) {
            printf("Thread affinity test failed\n");
        }
    }
    
    if (run_all || run_io) {
        if (test_device_io() < 0) {
            printf("Device I/O test failed\n");
        }
    }
    
    if (run_all || run_proc) {
        if (test_proc_interface() < 0) {
            printf("Proc interface test failed\n");
        }
    }
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
