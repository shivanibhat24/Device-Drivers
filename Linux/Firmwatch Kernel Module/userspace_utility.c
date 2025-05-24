#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>

#define DEVICE_PATH "/dev/firmwatch"

// IOCTL commands (must match kernel module)
#define FIRMWATCH_IOC_MAGIC 'F'
#define FIRMWATCH_LOAD_FIRMWARE    _IOW(FIRMWATCH_IOC_MAGIC, 1, struct firmware_load_req)
#define FIRMWATCH_UNLOAD_FIRMWARE  _IOW(FIRMWATCH_IOC_MAGIC, 2, int)
#define FIRMWATCH_LIST_FIRMWARE    _IOR(FIRMWATCH_IOC_MAGIC, 3, struct firmware_list)
#define FIRMWATCH_GET_INFO         _IOWR(FIRMWATCH_IOC_MAGIC, 4, struct firmware_info)

struct firmware_load_req {
    char name[256];
    size_t size;
    int slot_id;
};

struct firmware_info {
    int slot_id;
    char name[256];
    size_t size;
    unsigned long load_time;
    int ref_count;
};

struct firmware_list {
    int count;
    struct firmware_info entries[256];
};

static void print_usage(const char *prog_name)
{
    printf("Usage: %s [OPTIONS] COMMAND [ARGS]\n\n", prog_name);
    printf("Commands:\n");
    printf("  load <firmware_name> [slot_id]  Load firmware into slot\n");
    printf("  unload <slot_id>                Unload firmware from slot\n");
    printf("  list                            List all loaded firmware\n");
    printf("  info <slot_id>                  Get info about specific slot\n");
    printf("  mmap <slot_id> <output_file>    Memory map slot and dump to file\n");
    printf("  watch <slot_id>                 Watch slot for changes (hot-reload demo)\n");
    printf("\nOptions:\n");
    printf("  -h, --help                      Show this help message\n");
    printf("  -v, --verbose                   Verbose output\n");
    printf("\nExamples:\n");
    printf("  %s load my_firmware.bin         # Auto-assign slot\n", prog_name);
    printf("  %s load my_firmware.bin 5       # Load into slot 5\n", prog_name);
    printf("  %s unload 5                     # Unload slot 5\n", prog_name);
    printf("  %s list                         # List all firmware\n", prog_name);
    printf("  %s mmap 5 /tmp/firmware.dump    # Dump slot 5 to file\n", prog_name);
}

static int open_device(void)
{
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open " DEVICE_PATH);
        printf("Make sure the firmwatch kernel module is loaded.\n");
    }
    return fd;
}

static int load_firmware(const char *firmware_name, int slot_id)
{
    int fd, ret;
    struct firmware_load_req req;
    
    fd = open_device();
    if (fd < 0) return -1;
    
    memset(&req, 0, sizeof(req));
    strncpy(req.name, firmware_name, sizeof(req.name) - 1);
    req.slot_id = slot_id;
    
    ret = ioctl(fd, FIRMWATCH_LOAD_FIRMWARE, &req);
    if (ret < 0) {
        perror("Failed to load firmware");
        close(fd);
        return -1;
    }
    
    printf("Firmware '%s' loaded successfully into slot %d\n", 
           firmware_name, req.slot_id);
    
    close(fd);
    return req.slot_id;
}

static int unload_firmware(int slot_id)
{
    int fd, ret;
    
    fd = open_device();
    if (fd < 0) return -1;
    
    ret = ioctl(fd, FIRMWATCH_UNLOAD_FIRMWARE, &slot_id);
    if (ret < 0) {
        if (errno == ENOENT) {
            printf("Slot %d is not in use\n", slot_id);
        } else if (errno == EBUSY) {
            printf("Slot %d is busy (still mapped)\n", slot_id);
        } else {
            perror("Failed to unload firmware");
        }
        close(fd);
        return -1;
    }
    
    printf("Firmware unloaded from slot %d\n", slot_id);
    close(fd);
    return 0;
}

static int list_firmware(void)
{
    int fd, i;
    struct firmware_info info;
    
    fd = open_device();
    if (fd < 0) return -1;
    
    printf("Active Firmware Slots:\n");
    printf("======================\n");
    printf("%-4s %-32s %-12s %-8s %s\n", "Slot", "Name", "Size", "RefCount", "Load Time");
    printf("%-4s %-32s %-12s %-8s %s\n", "----", "----", "----", "--------", "---------");
    
    for (i = 0; i < 256; i++) {
        memset(&info, 0, sizeof(info));
        info.slot_id = i;
        
        if (ioctl(fd, FIRMWATCH_GET_INFO, &info) == 0) {
            time_t load_time = info.load_time / HZ; // Convert jiffies to seconds (approximate)
            char time_str[64];
            struct tm *tm_info = localtime(&load_time);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
            
            printf("%-4d %-32s %-12zu %-8d %s\n", 
                   info.slot_id, info.name, info.size, 
                   info.ref_count, time_str);
        }
    }
    
    close(fd);
    return 0;
}

static int get_firmware_info(int slot_id)
{
    int fd, ret;
    struct firmware_info info;
    
    fd = open_device();
    if (fd < 0) return -1;
    
    memset(&info, 0, sizeof(info));
    info.slot_id = slot_id;
    
    ret = ioctl(fd, FIRMWATCH_GET_INFO, &info);
    if (ret < 0) {
        if (errno == ENOENT) {
            printf("Slot %d is not in use\n", slot_id);
        } else {
            perror("Failed to get firmware info");
        }
        close(fd);
        return -1;
    }
    
    time_t load_time = info.load_time / HZ;
    char time_str[64];
    struct tm *tm_info = localtime(&load_time);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    printf("Firmware Info for Slot %d:\n", slot_id);
    printf("========================\n");
    printf("Name:       %s\n", info.name);
    printf("Size:       %zu bytes\n", info.size);
    printf("Ref Count:  %d\n", info.ref_count);
    printf("Load Time:  %s\n", time_str);
    
    close(fd);
    return 0;
}

static int mmap_firmware(int slot_id, const char *output_file)
{
    int fd, out_fd, ret = 0;
    struct firmware_info info;
    void *mapped_data;
    
    fd = open_device();
    if (fd < 0) return -1;
    
    // Get firmware info first
    memset(&info, 0, sizeof(info));
    info.slot_id = slot_id;
    
    if (ioctl(fd, FIRMWATCH_GET_INFO, &info) < 0) {
        if (errno == ENOENT) {
            printf("Slot %d is not in use\n", slot_id);
        } else {
            perror("Failed to get firmware info");
        }
        close(fd);
        return -1;
    }
    
    printf("Memory mapping slot %d (%zu bytes) to %s\n", 
           slot_id, info.size, output_file);
    
    // Memory map the firmware
    mapped_data = mmap(NULL, info.size, PROT_READ, MAP_SHARED, fd, slot_id);
    if (mapped_data == MAP_FAILED) {
        perror("Failed to mmap firmware");
        close(fd);
        return -1;
    }
    
    // Open output file
    out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        perror("Failed to open output file");
        munmap(mapped_data, info.size);
        close(fd);
        return -1;
    }
    
    // Copy data from mmap to file
    if (write(out_fd, mapped_data, info.size) != (ssize_t)info.size) {
        perror("Failed to write to output file");
        ret = -1;
    } else {
        printf("Successfully dumped %zu bytes to %s\n", info.size, output_file);
    }
    
    close(out_fd);
    munmap(mapped_data, info.size);
    close(fd);
    
    return ret;
}

static int watch_firmware(int slot_id)
{
    int fd;
    struct firmware_info info, prev_info;
    
    fd = open_device();
    if (fd < 0) return -1;
    
    // Get initial info
    memset(&prev_info, 0, sizeof(prev_info));
    prev_info.slot_id = slot_id;
    
    if (ioctl(fd, FIRMWATCH_GET_INFO, &prev_info) < 0) {
        if (errno == ENOENT) {
            printf("Slot %d is not in use\n", slot_id);
        } else {
            perror("Failed to get firmware info");
        }
        close(fd);
        return -1;
    }
    
    printf("Watching slot %d for changes (Ctrl+C to stop)...\n", slot_id);
    printf("Initial state: %s (%zu bytes)\n", prev_info.name, prev_info.size);
    
    while (1) {
        sleep(1);
        
        memset(&info, 0, sizeof(info));
        info.slot_id = slot_id;
        
        if (ioctl(fd, FIRMWATCH_GET_INFO, &info) < 0) {
            if (errno == ENOENT) {
                printf("Slot %d was unloaded\n", slot_id);
                break;
            }
            continue;
        }
        
        // Check for changes
        if (info.load_time != prev_info.load_time) {
            printf("FIRMWARE RELOADED: %s (%zu bytes)\n", info.name, info.size);
            prev_info = info;
        }
        
        if (info.ref_count != prev_info.ref_count) {
            printf("Reference count changed: %d -> %d\n", 
                   prev_info.ref_count, info.ref_count);
            prev_info.ref_count = info.ref_count;
        }
    }
    
    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    int opt, verbose = 0;
    
    static struct option long_options[] = {
        {"help",    no_argument, 0, 'h'},
        {"verbose", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "hv", long_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            print_usage(argv[0]);
            return 0;
        case 'v':
            verbose = 1;
            break;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (optind >= argc) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *command = argv[optind];
    
    if (strcmp(command, "load") == 0) {
        if (optind + 1 >= argc) {
            printf("Error: firmware name required\n");
            return 1;
        }
        
        const char *firmware_name = argv[optind + 1];
        int slot_id = -1; // Auto-assign by default
        
        if (optind + 2 < argc) {
            slot_id = atoi(argv[optind + 2]);
        }
        
        return load_firmware(firmware_name, slot_id) >= 0 ? 0 : 1;
        
    } else if (strcmp(command, "unload") == 0) {
        if (optind + 1 >= argc) {
            printf("Error: slot ID required\n");
            return 1;
        }
        
        int slot_id = atoi(argv[optind + 1]);
        return unload_firmware(slot_id);
        
    } else if (strcmp(command, "list") == 0) {
        return list_firmware();
        
    } else if (strcmp(command, "info") == 0) {
        if (optind + 1 >= argc) {
            printf("Error: slot ID required\n");
            return 1;
        }
        
        int slot_id = atoi(argv[optind + 1]);
        return get_firmware_info(slot_id);
        
    } else if (strcmp(command, "mmap") == 0) {
        if (optind + 2 >= argc) {
            printf("Error: slot ID and output file required\n");
            return 1;
        }
        
        int slot_id = atoi(argv[optind + 1]);
        const char *output_file = argv[optind + 2];
        return mmap_firmware(slot_id, output_file);
        
    } else if (strcmp(command, "watch") == 0) {
        if (optind + 1 >= argc) {
            printf("Error: slot ID required\n");
            return 1;
        }
        
        int slot_id = atoi(argv[optind + 1]);
        return watch_firmware(slot_id);
        
    } else {
        printf("Error: Unknown command '%s'\n", command);
        print_usage(argv[0]);
        return 1;
    }
}
