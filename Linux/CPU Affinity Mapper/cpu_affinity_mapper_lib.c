#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sched.h>
#include "cam.h"

/* Open the CAM device */
int cam_open_device(void) {
    int fd = open(CAM_DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("cam_open_device");
    }
    return fd;
}

/* Close the CAM device */
void cam_close_device(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

/* Set CPU affinity for a process or thread */
int cam_set_affinity(int fd, pid_t pid, pid_t tid, __u64 cpu_mask, __u32 policy) {
    struct cam_affinity_request req;
    
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    
    memset(&req, 0, sizeof(req));
    req.pid = pid;
    req.tid = tid;
    req.cpu_mask = cpu_mask;
    req.policy = policy;
    
    return ioctl(fd, CAM_SET_AFFINITY, &req);
}

/* Get CPU affinity information */
int cam_get_affinity(int fd, pid_t pid, pid_t tid, struct cam_affinity_info *info) {
    if (fd < 0 || !info) {
        errno = EINVAL;
        return -1;
    }
    
    memset(info, 0, sizeof(*info));
    info->pid = pid;
    info->tid = tid;
    
    return ioctl(fd, CAM_GET_AFFINITY, info);
}

/* Clear a CPU affinity mapping */
int cam_clear_mapping(int fd, pid_t pid, pid_t tid) {
    struct cam_clear_request req;
    
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    
    memset(&req, 0, sizeof(req));
    req.pid = pid;
    req.tid = tid;
    
    return ioctl(fd, CAM_CLEAR_MAPPING, &req);
}

/* Get system statistics */
int cam_get_stats(int fd, struct cam_mapping_stats *stats) {
    if (fd < 0 || !stats) {
        errno = EINVAL;
        return -1;
    }
    
    return ioctl(fd, CAM_GET_STATS, stats);
}

/* Reset statistics */
int cam_reset_stats(int fd) {
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    
    return ioctl(fd, CAM_RESET_STATS);
}

/* List active mappings */
int cam_list_mappings(int fd, struct cam_mapping_list *list) {
    if (fd < 0 || !list) {
        errno = EINVAL;
        return -1;
    }
    
    return ioctl(fd, CAM_LIST_MAPPINGS, list);
}

/* Get human-readable policy name */
const char *cam_policy_name(__u32 policy) {
    switch (policy) {
    case CAM_POLICY_STRICT:
        return "strict";
    case CAM_POLICY_PREFERRED:
        return "preferred";
    case CAM_POLICY_BALANCED:
        return "balanced";
    default:
        return "unknown";
    }
}

/* Get human-readable status name */
const char *cam_status_name(__s32 status) {
    switch (status) {
    case CAM_STATUS_SUCCESS:
        return "success";
    case CAM_STATUS_NOT_FOUND:
        return "not found";
    case CAM_STATUS_INVALID:
        return "invalid";
    case CAM_STATUS_NO_MEMORY:
        return "no memory";
    case CAM_STATUS_PERMISSION:
        return "permission denied";
    default:
        return "unknown error";
    }
}

/* Print CPU mask in human-readable format */
void cam_print_cpu_mask(__u64 mask) {
    int first = 1;
    
    printf("{");
    for (int cpu = 0; cpu < CAM_MAX_CPUS; cpu++) {
        if (CAM_CPU_ISSET(cpu, mask)) {
            if (!first) {
                printf(",");
            }
            printf("%d", cpu);
            first = 0;
        }
    }
    printf("}");
}

/* Parse CPU list string (e.g., "0,2-4,7") into CPU mask */
__u64 cam_parse_cpu_list(const char *cpu_list) {
    __u64 mask = 0;
    char *str, *token, *saveptr;
    char *range_start, *range_end;
    int start_cpu, end_cpu;
    
    if (!cpu_list || strlen(cpu_list) == 0) {
        return 0;
    }
    
    /* Make a copy since strtok modifies the string */
    str = strdup(cpu_list);
    if (!str) {
        return 0;
    }
    
    token = strtok_r(str, ",", &saveptr);
    while (token) {
        /* Check if it's a range (contains '-') */
        range_start = token;
        range_end = strchr(token, '-');
        
        if (range_end) {
            /* It's a range */
            *range_end = '\0';
            range_end++;
            
            start_cpu = atoi(range_start);
            end_cpu = atoi(range_end);
            
            if (start_cpu >= 0 && end_cpu >= start_cpu && 
                start_cpu < CAM_MAX_CPUS && end_cpu < CAM_MAX_CPUS) {
                for (int cpu = start_cpu; cpu <= end_cpu; cpu++) {
                    CAM_CPU_SET(cpu, mask);
                }
            }
        } else {
            /* Single CPU */
            int cpu = atoi(token);
            if (cpu >= 0 && cpu < CAM_MAX_CPUS) {
                CAM_CPU_SET(cpu, mask);
            }
        }
        
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    free(str);
    return mask;
}

/* Get mask of online CPUs */
int cam_get_online_cpus(__u64 *mask) {
    cpu_set_t cpu_set;
    int num_cpus;
    
    if (!mask) {
        errno = EINVAL;
        return -1;
    }
    
    CPU_ZERO(&cpu_set);
    
    /* Get the affinity mask to see which CPUs are available */
    if (sched_getaffinity(0, sizeof(cpu_set), &cpu_set) != 0) {
        return -1;
    }
    
    *mask = 0;
    num_cpus = 0;
    
    for (int cpu = 0; cpu < CAM_MAX_CPUS && cpu < CPU_SETSIZE; cpu++) {
        if (CPU_ISSET(cpu, &cpu_set)) {
            CAM_CPU_SET(cpu, *mask);
            num_cpus++;
        }
    }
    
    return num_cpus;
}

/* High-level convenience functions */

/* Set affinity for current process */
int cam_set_self_affinity(__u64 cpu_mask, __u32 policy) {
    int fd, ret;
    
    fd = cam_open_device();
    if (fd < 0) {
        return -1;
    }
    
    ret = cam_set_affinity(fd, getpid(), 0, cpu_mask, policy);
    cam_close_device(fd);
    
    return ret;
}

/* Set affinity for current thread */
int cam_set_thread_affinity(__u64 cpu_mask, __u32 policy) {
    int fd, ret;
    
    fd = cam_open_device();
    if (fd < 0) {
        return -1;
    }
    
    ret = cam_set_affinity(fd, getpid(), gettid(), cpu_mask, policy);
    cam_close_device(fd);
    
    return ret;
}

/* Set affinity by CPU list string */
int cam_set_affinity_by_list(pid_t pid, pid_t tid, const char *cpu_list, __u32 policy) {
    __u64 mask;
    int fd, ret;
    
    mask = cam_parse_cpu_list(cpu_list);
    if (mask == 0) {
        errno = EINVAL;
        return -1;
    }
    
    fd = cam_open_device();
    if (fd < 0) {
        return -1;
    }
    
    ret = cam_set_affinity(fd, pid, tid, mask, policy);
    cam_close_device(fd);
    
    return ret;
}

/* Print detailed affinity information */
void cam_print_affinity_info(const struct cam_affinity_info *info) {
    if (!info) {
        return;
    }
    
    printf("Affinity Information:\n");
    printf("  PID: %d\n", info->pid);
    printf("  TID: %d\n", info->tid);
    printf("  Status: %s (%d)\n", cam_status_name(info->status), info->status);
    
    if (info->status == CAM_STATUS_SUCCESS) {
        printf("  Current CPU mask: ");
        cam_print_cpu_mask(info->current_mask);
        printf("\n");
        
        printf("  Requested CPU mask: ");
        cam_print_cpu_mask(info->requested_mask);
        printf("\n");
        
        printf("  Policy: %s (%u)\n", cam_policy_name(info->policy), info->policy);
        printf("  Switch count: %llu\n", (unsigned long long)info->switch_count);
        printf("  Last update: %llu\n", (unsigned long long)info->last_update);
    }
}

/* Print system statistics */
void cam_print_stats(const struct cam_mapping_stats *stats) {
    if (!stats) {
        return;
    }
    
    printf("CPU Affinity Mapper Statistics:\n");
    printf("  Total mappings created: %u\n", stats->total_mappings);
    printf("  Active mappings: %u\n", stats->active_mappings);
    printf("  Failed mappings: %u\n", stats->failed_mappings);
    printf("  Cleared mappings: %u\n", stats->cleared_mappings);
    printf("  Total affinity switches: %llu\n", (unsigned long long)stats->total_switches);
    printf("  Maximum mappings allowed: %u\n", stats->max_mappings);
    printf("  Available CPUs: %u\n", stats->available_cpus);
    printf("  Last reset: %llu\n", (unsigned long long)stats->last_reset);
}

/* Validate CPU mask against online CPUs */
int cam_validate_cpu_mask(__u64 mask) {
    __u64 online_mask;
    int num_online;
    
    if (mask == 0) {
        return 0;  /* Empty mask is invalid */
    }
    
    num_online = cam_get_online_cpus(&online_mask);
    if (num_online < 0) {
        return -1;  /* Error getting online CPUs */
    }
    
    /* Check if all requested CPUs are online */
    if ((mask & online_mask) != mask) {
        return 0;  /* Some requested CPUs are offline */
    }
    
    return 1;  /* Valid mask */
}

/* Get number of CPUs in mask */
int cam_count_cpus(__u64 mask) {
    int count = 0;
    
    for (int cpu = 0; cpu < CAM_MAX_CPUS; cpu++) {
        if (CAM_CPU_ISSET(cpu, mask)) {
            count++;
        }
    }
    
    return count;
}
