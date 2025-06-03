/*
 * sandboxctl.c - Userland control tool for Linux Kernel Driver Sandbox
 * 
 * Command-line utility to manage sandboxed kernel drivers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <pthread.h>

#define SANDBOX_DEVICE "/dev/sandbox_control"
#define SANDBOX_PROC_STATUS "/proc/sandbox/status"
#define SANDBOX_PROC_IRQCTL "/proc/sandbox/irqctl"
#define SANDBOX_LOG_PATH "/sys/kernel/debug/sandbox/sandbox_log0"

/* IOCTL commands - must match sandbox_api.h */
#define SANDBOX_IOCTL_MAGIC 'S'
#define SANDBOX_IOCTL_LOAD_DRIVER    _IOW(SANDBOX_IOCTL_MAGIC, 1, char*)
#define SANDBOX_IOCTL_UNLOAD_DRIVER  _IOW(SANDBOX_IOCTL_MAGIC, 2, char*)
#define SANDBOX_IOCTL_TRACE_START    _IOW(SANDBOX_IOCTL_MAGIC, 3, char*)
#define SANDBOX_IOCTL_TRACE_STOP     _IOW(SANDBOX_IOCTL_MAGIC, 4, char*)
#define SANDBOX_IOCTL_FUZZ_START     _IOW(SANDBOX_IOCTL_MAGIC, 5, struct sandbox_fuzz_config*)
#define SANDBOX_IOCTL_FUZZ_STOP      _IO(SANDBOX_IOCTL_MAGIC, 6)

/* Structures */
struct sandbox_fuzz_config {
    char driver_name[64];
    unsigned int modes;
    unsigned int duration_ms;
    unsigned int intensity;
};

/* Fuzz modes */
#define SANDBOX_FUZZ_IO     (1 << 0)
#define SANDBOX_FUZZ_IRQ    (1 << 1)
#define SANDBOX_FUZZ_MMAP   (1 << 2)
#define SANDBOX_FUZZ_IOCTL  (1 << 3)

/* Global variables */
static int sandbox_fd = -1;
static volatile int stop_tracing = 0;
static volatile int stop_fuzzing = 0;

/* Function prototypes */
static void print_usage(const char *progname);
static int load_driver(const char *driver_path);
static int unload_driver(const char *driver_name);
static int run_test(const char *driver_name, char **args);
static int start_trace(const char *driver_name);
static int fuzz_driver(const char *driver_name, const char *modes, 
                      int duration, int intensity);
static int show_status(void);
static void signal_handler(int sig);
static void *trace_thread(void *arg);
static void *fuzz_thread(void *arg);

/* Print usage information */
static void print_usage(const char *progname)
{
    printf("Usage: %s <command> [options]\n", progname);
    printf("\nCommands:\n");
    printf("  load <driver.ko>              Load a driver module into sandbox\n");
    printf("  unload <driver_name>          Unload a driver from sandbox\n");
    printf("  run <driver> [--args ...]     Run test interaction with driver\n");
    printf("  trace <driver>                Start live tracing of driver calls\n");
    printf("  fuzz <driver> [options]       Fuzz test the driver\n");
    printf("  status                        Show sandbox status\n");
    printf("  irq <enable|disable>          Control IRQ simulation\n");
    printf("\nFuzz options:\n");
    printf("  --modes=io,irq,mmap,ioctl     Fuzz modes (default: io)\n");
    printf("  --duration=<seconds>          Fuzz duration (default: 30)\n");
    printf("  --intensity=<1-10>            Fuzz intensity (default: 5)\n");
    printf("\nExamples:\n");
    printf("  %s load /path/to/driver.ko\n", progname);
    printf("  %s trace mydriver\n", progname);
    printf("  %s fuzz mydriver --modes=io,ioctl --duration=60\n", progname);
}

/* Load driver into sandbox */
static int load_driver(const char *driver_path)
{
    char cmd[512];
    char driver_name[64];
    char *basename_ptr;
    int ret;
    
    if (!driver_path) {
        fprintf(stderr, "Error: Driver path required\n");
        return -1;
    }
    
    /* Extract driver name from path */
    basename_ptr = strrchr(driver_path, '/');
    if (basename_ptr) {
        basename_ptr++;
    } else {
        basename_ptr = (char*)driver_path;
    }
    
    strncpy(driver_name, basename_ptr, sizeof(driver_name) - 1);
    driver_name[sizeof(driver_name) - 1] = '\0';
    
    /* Remove .ko extension if present */
    char *ext = strstr(driver_name, ".ko");
    if (ext) {
        *ext = '\0';
    }
    
    printf("Loading driver: %s\n", driver_path);
    
    /* Use insmod to load the module */
    snprintf(cmd, sizeof(cmd), "insmod %s sandbox=1", driver_path);
    ret = system(cmd);
    
    if (ret == 0) {
        printf("Driver '%s' loaded successfully into sandbox\n", driver_name);
    } else {
        fprintf(stderr, "Failed to load driver: %s\n", strerror(errno));
        return -1;
    }
    
    return 0;
}

/* Unload driver from sandbox */
static int unload_driver(const char *driver_name)
{
    char cmd[256];
    int ret;
    
    if (!driver_name) {
        fprintf(stderr, "Error: Driver name required\n");
        return -1;
    }
    
    printf("Unloading driver: %s\n", driver_name);
    
    /* Use rmmod to unload the module */
    snprintf(cmd, sizeof(cmd), "rmmod %s", driver_name);
    ret = system(cmd);
    
    if (ret == 0) {
        printf("Driver '%s' unloaded successfully\n", driver_name);
    } else {
        fprintf(stderr, "Failed to unload driver: %s\n", strerror(errno));
        return -1;
    }
    
    return 0;
}

/* Run test interaction with driver */
static int run_test(const char *driver_name, char **args)
{
    char device_path[256];
    int fd, ret = 0;
    char buffer[1024];
    ssize_t bytes;
    
    if (!driver_name) {
        fprintf(stderr, "Error: Driver name required\n");
        return -1;
    }
    
    printf("Running test for driver: %s\n", driver_name);
    
    /* Try to open the sandbox device */
    snprintf(device_path, sizeof(device_path), "/dev/sandbox_%s", driver_name);
    fd = open(device_path, O_RDWR);
    
    if (fd < 0) {
        fprintf(stderr, "Failed to open device %s: %s\n", 
                device_path, strerror(errno));
        return -1;
    }
    
    printf("Device opened successfully: %s\n", device_path);
    
    /* Basic read/write test */
    const char *test_data = "Hello from sandboxctl!";
    bytes = write(fd, test_data, strlen(test_data));
    if (bytes > 0) {
        printf("Write test: %zd bytes written\n", bytes);
    }
    
    bytes = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Read test: %zd bytes read: '%s'\n", bytes, buffer);
    }
    
    /* IOCTL test */
    ret = ioctl(fd, 0x1000, 0x12345678);
    printf("IOCTL test: result = %d\n", ret);
    
    close(fd);
    printf("Test completed for driver: %s\n", driver_name);
    return 0;
}

/* Start tracing driver calls */
static int start_trace(const char *driver_name)
{
    pthread_t trace_tid;
    int ret;
    
    if (!driver_name) {
        fprintf(stderr, "Error: Driver name required\n");
        return -1;
    }
    
    printf("Starting trace for driver: %s\n", driver_name);
    printf("Press Ctrl+C to stop tracing...\n");
    
    /* Install signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Create trace thread */
    ret = pthread_create(&trace_tid, NULL, trace_thread, (void*)driver_name);
    if (ret != 0) {
        fprintf(stderr, "Failed to create trace thread: %s\n", strerror(ret));
        return -1;
    }
    
    /* Wait for thread to complete */
    pthread_join(trace_tid, NULL);
    
    printf("Tracing stopped\n");
    return 0;
}

/* Fuzz test driver */
static int fuzz_driver(const char *driver_name, const char *modes, 
                      int duration, int intensity)
{
    struct sandbox_fuzz_config config;
    pthread_t fuzz_tid;
    int ret;
    
    if (!driver_name) {
        fprintf(stderr, "Error: Driver name required\n");
        return -1;
    }
    
    /* Parse fuzz modes */
    memset(&config, 0, sizeof(config));
    strncpy(config.driver_name, driver_name, sizeof(config.driver_name) - 1);
    config.duration_ms = duration * 1000;
    config.intensity = intensity;
    
    if (modes) {
        if (strstr(modes, "io")) config.modes |= SANDBOX_FUZZ_IO;
        if (strstr(modes, "irq")) config.modes |= SANDBOX_FUZZ_IRQ;
        if (strstr(modes, "mmap")) config.modes |= SANDBOX_FUZZ_MMAP;
        if (strstr(modes, "ioctl")) config.modes |= SANDBOX_FUZZ_IOCTL;
    } else {
        config.modes = SANDBOX_FUZZ_IO; /* Default */
    }
    
    printf("Starting fuzz test for driver: %s\n", driver_name);
    printf("Modes: 0x%x, Duration: %d seconds, Intensity: %d\n",
           config.modes, duration, intensity);
    printf("Press Ctrl+C to stop fuzzing...\n");
    
    /* Install signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Create fuzz thread */
    ret = pthread_create(&fuzz_tid, NULL, fuzz_thread, &config);
    if (ret != 0) {
        fprintf(stderr, "Failed to create fuzz thread: %s\n", strerror(ret));
        return -1;
    }
    
    /* Wait for thread to complete */
    pthread_join(fuzz_tid, NULL);
    
    printf("Fuzzing stopped\n");
    return 0;
}

/* Show sandbox status */
static int show_status(void)
{
    FILE *fp;
    char line[256];
    
    fp = fopen(SANDBOX_PROC_STATUS, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open %s: %s\n", 
                SANDBOX_PROC_STATUS, strerror(errno));
        return -1;
    }
    
    printf("Sandbox Status:\n");
    printf("===============\n");
    
    while (fgets(line, sizeof(line), fp)) {
        printf("%s", line);
    }
    
    fclose(fp);
    return 0;
}

/* Signal handler for graceful shutdown */
static void signal_handler(int sig)
{
    stop_tracing = 1;
    stop_fuzzing = 1;
    printf("\nReceived signal %d, stopping...\n", sig);
}

/* Trace thread function */
static void *trace_thread(void *arg)
{
    const char *driver_name = (const char*)arg;
    FILE *fp;
    char buffer[1024];
    size_t bytes_read;
    
    /* Open sandbox log file */
    fp = fopen(SANDBOX_LOG_PATH, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open log file: %s\n", strerror(errno));
        return NULL;
    }
    
    /* Seek to end of file to get new entries */
    fseek(fp, 0, SEEK_END);
    
    printf("=== Live Trace Output ===\n");
    
    while (!stop_tracing) {
        bytes_read = fread(buffer, 1, sizeof(buffer) - 1, fp);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        } else {
            usleep(100000); /* Sleep 100ms */
        }
    }
    
    fclose(fp);
    return NULL;
}

/* Fuzz thread function */
static void *fuzz_thread(void *arg)
{
    struct sandbox_fuzz_config *config = (struct sandbox_fuzz_config*)arg;
    char device_path[256];
    int fd, i;
    time_t start_time;
    unsigned char fuzz_data[1024];
    
    snprintf(device_path, sizeof(device_path), "/dev/sandbox_%s", config->driver_name);
    
    fd = open(device_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open device for fuzzing: %s\n", strerror(errno));
        return NULL;
    }
    
    start_time = time(NULL);
    srand(start_time);
    
    printf("=== Fuzz Test Started ===\n");
    
    i = 0;
    while (!stop_fuzzing && (time(NULL) - start_time) < (config->duration_ms / 1000)) {
        /* Generate random data */
        for (int j = 0; j < sizeof(fuzz_data); j++) {
            fuzz_data[j] = rand() % 256;
        }
        
        if (config->modes & SANDBOX_FUZZ_IO) {
            /* Fuzz read/write operations */
            write(fd, fuzz_data, rand() % sizeof(fuzz_data));
            read(fd, fuzz_data, rand() % sizeof(fuzz_data));
        }
        
        if (config->modes & SANDBOX_FUZZ_IOCTL) {
            /* Fuzz IOCTL operations */
            unsigned int cmd = rand();
            unsigned long arg = rand();
            ioctl(fd, cmd, arg);
        }
        
        if (config->modes & SANDBOX_FUZZ_MMAP) {
            /* Fuzz memory mapping */
            void *addr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, 
                             MAP_SHARED, fd, 0);
            if (addr != MAP_FAILED) {
                munmap(addr, 4096);
            }
        }
        
        i++;
        if (i % 100 == 0) {
            printf("Fuzz iterations: %d\n", i);
        }
        
        /* Sleep based on intensity (lower intensity = longer sleep) */
        usleep(1000 * (11 - config->intensity));
    }
    
    close(fd);
    printf("=== Fuzz Test Completed ===\n");
    printf("Total iterations: %d\n", i);
    
    return NULL;
}

/* Control IRQ simulation */
static int control_irq(const char *action)
{
    FILE *fp;
    
    if (!action) {
        fprintf(stderr, "Error: IRQ action required (enable/disable)\n");
        return -1;
    }
    
    fp = fopen(SANDBOX_PROC_IRQCTL, "w");
    if (!fp) {
        fprintf(stderr, "Failed to open %s: %s\n", 
                SANDBOX_PROC_IRQCTL, strerror(errno));
        return -1;
    }
    
    fprintf(fp, "%s", action);
    fclose(fp);
    
    printf("IRQ simulation %sd\n", action);
    return 0;
}

/* Main function */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *command = argv[1];
    
    if (strcmp(command, "load") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Driver path required\n");
            return 1;
        }
        return load_driver(argv[2]);
        
    } else if (strcmp(command, "unload") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Driver name required\n");
            return 1;
        }
        return unload_driver(argv[2]);
        
    } else if (strcmp(command, "run") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Driver name required\n");
            return 1;
        }
        return run_test(argv[2], &argv[3]);
        
    } else if (strcmp(command, "trace") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Driver name required\n");
            return 1;
        }
        return start_trace(argv[2]);
        
    } else if (strcmp(command, "fuzz") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: Driver name required\n");
            return 1;
        }
        
        /* Parse fuzz options */
        const char *modes = "io";
        int duration = 30;
        int intensity = 5;
        
        for (int i = 3; i < argc; i++) {
            if (strncmp(argv[i], "--modes=", 8) == 0) {
                modes = argv[i] + 8;
            } else if (strncmp(argv[i], "--duration=", 11) == 0) {
                duration = atoi(argv[i] + 11);
            } else if (strncmp(argv[i], "--intensity=", 12) == 0) {
                intensity = atoi(argv[i] + 12);
                if (intensity < 1) intensity = 1;
                if (intensity > 10) intensity = 10;
            }
        }
        
        return fuzz_driver(argv[2], modes, duration, intensity);
        
    } else if (strcmp(command, "status") == 0) {
        return show_status();
        
    } else if (strcmp(command, "irq") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: IRQ action required (enable/disable)\n");
            return 1;
        }
        return control_irq(argv[2]);
        
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command);
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
