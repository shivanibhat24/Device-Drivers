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
    
    printf("Unloading driver
