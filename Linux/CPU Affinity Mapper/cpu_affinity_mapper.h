#ifndef _CAM_H_
#define _CAM_H_

#include <linux/ioctl.h>
#include <linux/types.h>

/* Device information */
#define CAM_DEVICE_NAME "cpu_affinity_mapper"
#define CAM_DEVICE_PATH "/dev/cpu_affinity_mapper"
#define CAM_PROC_PATH "/proc/cpu_affinity_mapper"
#define CAM_CLASS_NAME "cam"
#define CAM_MAX_MAPPINGS 1024

/* IOCTL Magic Number */
#define CAM_MAGIC 'C'

/* IOCTL Commands */
#define CAM_SET_AFFINITY    _IOW(CAM_MAGIC, 1, struct cam_affinity_request)
#define CAM_GET_AFFINITY    _IOR(CAM_MAGIC, 2, struct cam_affinity_info)
#define CAM_CLEAR_MAPPING   _IOW(CAM_MAGIC, 3, struct cam_clear_request)
#define CAM_GET_STATS       _IOR(CAM_MAGIC, 4, struct cam_mapping_stats)
#define CAM_SET_POLICY      _IOW(CAM_MAGIC, 5, struct cam_policy_request)
#define CAM_RESET_STATS     _IO(CAM_MAGIC, 6)
#define CAM_LIST_MAPPINGS   _IOR(CAM_MAGIC, 7, struct cam_mapping_list)

/* Affinity Policies */
#define CAM_POLICY_STRICT    0  /* Strict affinity - fail if CPUs unavailable */
#define CAM_POLICY_PREFERRED 1  /* Preferred affinity - fallback to available CPUs */
#define CAM_POLICY_BALANCED  2  /* Load balanced across specified CPUs */

/* Status Codes */
#define CAM_STATUS_SUCCESS    0
#define CAM_STATUS_NOT_FOUND -1
#define CAM_STATUS_INVALID   -2
#define CAM_STATUS_NO_MEMORY -3
#define CAM_STATUS_PERMISSION -4

/* Data Structures */

/* Request to set CPU affinity */
struct cam_affinity_request {
    pid_t pid;                /* Process ID */
    pid_t tid;               /* Thread ID (0 for entire process) */
    __u64 cpu_mask;          /* CPU mask (bitmask of allowed CPUs) */
    __u32 policy;            /* Affinity policy */
    __u32 flags;             /* Additional flags */
    __u64 reserved[2];       /* Reserved for future use */
};

/* Information about current affinity */
struct cam_affinity_info {
    pid_t pid;               /* Process ID */
    pid_t tid;               /* Thread ID */
    __u64 current_mask;      /* Current CPU mask */
    __u64 requested_mask;    /* Requested CPU mask */
    __u32 policy;            /* Current policy */
    __s32 status;            /* Status code */
    __u64 switch_count;      /* Number of affinity switches */
    __u64 last_update;       /* Timestamp of last update */
    __u64 reserved[2];       /* Reserved for future use */
};

/* Request to clear mappings */
struct cam_clear_request {
    pid_t pid;               /* Process ID (0 for all processes) */
    pid_t tid;               /* Thread ID (0 for all threads in process) */
    __u32 flags;             /* Clear flags */
    __u32 reserved;          /* Reserved */
};

/* System statistics */
struct cam_mapping_stats {
    __u32 total_mappings;    /* Total mappings created */
    __u32 active_mappings;   /* Currently active mappings */
    __u32 failed_mappings;   /* Failed mapping attempts */
    __u32 cleared_mappings;  /* Mappings that were cleared */
    __u64 total_switches;    /* Total affinity switches performed */
    __u64 last_reset;        /* Last statistics reset timestamp */
    __u32 max_mappings;      /* Maximum allowed mappings */
    __u32 available_cpus;    /* Number of available CPUs */
    __u64 reserved[4];       /* Reserved for future use */
};

/* Policy configuration */
struct cam_policy_request {
    __u32 global_policy;     /* Global default policy */
    __u32 max_mappings;      /* Maximum allowed mappings */
    __u32 flags;             /* Policy flags */
    __u32 reserved;          /* Reserved */
};

/* Single mapping entry for listing */
struct cam_mapping_entry {
    pid_t pid;               /* Process ID */
    pid_t tid;               /* Thread ID */
    __u64 cpu_mask;          /* CPU mask */
    __u32 policy;            /* Policy */
    __u32 status;            /* Current status */
    __u64 switch_count;      /* Number of switches */
    __u64 create_time;       /* Creation timestamp */
    __u64 last_update;       /* Last update timestamp */
    char comm[16];           /* Process command name */
};

/* List of mappings */
struct cam_mapping_list {
    __u32 count;             /* Number of entries requested/returned */
    __u32 total;             /* Total number of mappings */
    __u32 offset;            /* Offset for pagination */
    __u32 reserved;          /* Reserved */
    struct cam_mapping_entry *entries; /* Array of mapping entries */
};

/* Utility macros for CPU mask manipulation */
#define CAM_CPU_SET(cpu, mask)    ((mask) |= (1ULL << (cpu)))
#define CAM_CPU_CLR(cpu, mask)    ((mask) &= ~(1ULL << (cpu)))
#define CAM_CPU_ISSET(cpu, mask)  (((mask) & (1ULL << (cpu))) != 0)
#define CAM_CPU_ZERO(mask)        ((mask) = 0ULL)
#define CAM_CPU_SETALL(mask)      ((mask) = ~0ULL)

/* Maximum number of CPUs supported (64-bit mask) */
#define CAM_MAX_CPUS 64

/* Flags for affinity requests */
#define CAM_FLAG_PERSISTENT   0x01  /* Keep mapping across exec */
#define CAM_FLAG_INHERIT      0x02  /* Child processes inherit mapping */
#define CAM_FLAG_MONITOR      0x04  /* Monitor and log affinity changes */

/* Flags for clear requests */
#define CAM_CLEAR_FLAG_FORCE  0x01  /* Force clear even if in use */

#ifdef __KERNEL__
/* Kernel-only definitions */

struct cam_thread_mapping {
    struct list_head list;
    pid_t pid;
    pid_t tid;
    __u64 cpu_mask;
    __u32 policy;
    __u32 flags;
    __u64 switch_count;
    ktime_t create_time;
    ktime_t last_update;
    struct task_struct *task;  /* Cached task pointer */
    char comm[TASK_COMM_LEN];  /* Process name */
};

/* Internal kernel functions */
int cam_apply_affinity(struct cam_thread_mapping *mapping);
struct cam_thread_mapping *cam_find_mapping(pid_t pid, pid_t tid);
int cam_add_mapping(struct cam_affinity_request *req);
int cam_remove_mapping(pid_t pid, pid_t tid);
void cam_cleanup_dead_mappings(void);

#endif /* __KERNEL__ */

/* Userspace utility functions */
#ifdef __cplusplus
extern "C" {
#endif

/* Library functions for userspace applications */
int cam_open_device(void);
void cam_close_device(int fd);
int cam_set_affinity(int fd, pid_t pid, pid_t tid, __u64 cpu_mask, __u32 policy);
int cam_get_affinity(int fd, pid_t pid, pid_t tid, struct cam_affinity_info *info);
int cam_clear_mapping(int fd, pid_t pid, pid_t tid);
int cam_get_stats(int fd, struct cam_mapping_stats *stats);
int cam_reset_stats(int fd);
int cam_list_mappings(int fd, struct cam_mapping_list *list);

/* Utility functions */
const char *cam_policy_name(__u32 policy);
const char *cam_status_name(__s32 status);
void cam_print_cpu_mask(__u64 mask);
__u64 cam_parse_cpu_list(const char *cpu_list);
int cam_get_online_cpus(__u64 *mask);

#ifdef __cplusplus
}
#endif

#endif /* _CAM_H_ */
