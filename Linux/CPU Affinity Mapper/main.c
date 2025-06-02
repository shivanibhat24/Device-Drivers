#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/cpumask.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define DEVICE_NAME "cpu_affinity_mapper"
#define CLASS_NAME "cam"
#define MAX_MAPPINGS 1024

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("CPU Affinity Mapper Device Driver");
MODULE_VERSION("1.0");

/* IOCTL commands */
#define CAM_MAGIC 'C'
#define CAM_SET_AFFINITY    _IOW(CAM_MAGIC, 1, struct affinity_request)
#define CAM_GET_AFFINITY    _IOR(CAM_MAGIC, 2, struct affinity_info)
#define CAM_CLEAR_MAPPING   _IOW(CAM_MAGIC, 3, pid_t)
#define CAM_GET_STATS       _IOR(CAM_MAGIC, 4, struct mapping_stats)

/* Data structures */
struct affinity_request {
    pid_t pid;
    pid_t tid;  // Thread ID (0 for process)
    unsigned long cpu_mask;
    int policy;  // 0=strict, 1=preferred
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

struct thread_mapping {
    struct list_head list;
    pid_t pid;
    pid_t tid;
    unsigned long cpu_mask;
    int policy;
    unsigned long switches;
    ktime_t last_update;
};

/* Global variables */
static dev_t dev_num;
static struct class *cam_class;
static struct device *cam_device;
static struct cdev cam_cdev;
static DEFINE_MUTEX(mappings_mutex);
static LIST_HEAD(mappings_list);
static struct mapping_stats stats = {0};
static struct proc_dir_entry *proc_entry;

/* Function prototypes */
static int cam_open(struct inode *inode, struct file *file);
static int cam_release(struct inode *inode, struct file *file);
static long cam_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static ssize_t cam_read(struct file *file, char __user *buffer, size_t len, loff_t *offset);
static ssize_t cam_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset);

/* File operations */
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = cam_open,
    .release = cam_release,
    .unlocked_ioctl = cam_ioctl,
    .read = cam_read,
    .write = cam_write,
};

/* Helper functions */
static struct thread_mapping *find_mapping(pid_t pid, pid_t tid)
{
    struct thread_mapping *mapping;
    
    list_for_each_entry(mapping, &mappings_list, list) {
        if (mapping->pid == pid && mapping->tid == tid) {
            return mapping;
        }
    }
    return NULL;
}

static int apply_cpu_affinity(pid_t pid, pid_t tid, unsigned long cpu_mask)
{
    struct task_struct *task;
    struct pid *pid_struct;
    cpumask_t new_mask;
    int ret = 0;
    
    /* Convert bitmask to cpumask */
    cpumask_clear(&new_mask);
    
    for (int cpu = 0; cpu < nr_cpu_ids && cpu < sizeof(cpu_mask) * 8; cpu++) {
        if (cpu_mask & (1UL << cpu)) {
            if (cpu_online(cpu)) {
                cpumask_set_cpu(cpu, &new_mask);
            }
        }
    }
    
    if (cpumask_empty(&new_mask)) {
        printk(KERN_WARNING "CAM: No valid CPUs in mask 0x%lx\n", cpu_mask);
        return -EINVAL;
    }
    
    /* Find the task */
    rcu_read_lock();
    
    if (tid == 0) {
        /* Process affinity */
        pid_struct = find_vpid(pid);
        if (!pid_struct) {
            rcu_read_unlock();
            return -ESRCH;
        }
        task = pid_task(pid_struct, PIDTYPE_PID);
    } else {
        /* Thread affinity */
        pid_struct = find_vpid(tid);
        if (!pid_struct) {
            rcu_read_unlock();
            return -ESRCH;
        }
        task = pid_task(pid_struct, PIDTYPE_PID);
        
        /* Verify thread belongs to process */
        if (!task || task->tgid != pid) {
            rcu_read_unlock();
            return -ESRCH;
        }
    }
    
    if (!task) {
        rcu_read_unlock();
        return -ESRCH;
    }
    
    get_task_struct(task);
    rcu_read_unlock();
    
    /* Apply the affinity */
    ret = set_cpus_allowed_ptr(task, &new_mask);
    
    put_task_struct(task);
    
    if (ret == 0) {
        stats.total_switches++;
        printk(KERN_INFO "CAM: Set affinity for PID=%d TID=%d to mask=0x%lx\n", 
               pid, tid, cpu_mask);
    } else {
        printk(KERN_WARNING "CAM: Failed to set affinity for PID=%d TID=%d: %d\n", 
               pid, tid, ret);
    }
    
    return ret;
}

static int add_mapping(struct affinity_request *req)
{
    struct thread_mapping *mapping;
    int ret;
    
    mutex_lock(&mappings_mutex);
    
    /* Check if mapping already exists */
    mapping = find_mapping(req->pid, req->tid);
    if (mapping) {
        /* Update existing mapping */
        mapping->cpu_mask = req->cpu_mask;
        mapping->policy = req->policy;
        mapping->last_update = ktime_get();
    } else {
        /* Create new mapping */
        if (stats.total_mappings >= MAX_MAPPINGS) {
            mutex_unlock(&mappings_mutex);
            return -ENOMEM;
        }
        
        mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
        if (!mapping) {
            mutex_unlock(&mappings_mutex);
            return -ENOMEM;
        }
        
        mapping->pid = req->pid;
        mapping->tid = req->tid;
        mapping->cpu_mask = req->cpu_mask;
        mapping->policy = req->policy;
        mapping->switches = 0;
        mapping->last_update = ktime_get();
        
        list_add_tail(&mapping->list, &mappings_list);
        stats.total_mappings++;
        stats.active_mappings++;
    }
    
    mutex_unlock(&mappings_mutex);
    
    /* Apply the affinity */
    ret = apply_cpu_affinity(req->pid, req->tid, req->cpu_mask);
    
    if (ret != 0) {
        stats.failed_mappings++;
    } else {
        mapping->switches++;
    }
    
    return ret;
}

static int remove_mapping(pid_t pid, pid_t tid)
{
    struct thread_mapping *mapping;
    
    mutex_lock(&mappings_mutex);
    
    mapping = find_mapping(pid, tid);
    if (mapping) {
        list_del(&mapping->list);
        kfree(mapping);
        stats.active_mappings--;
        mutex_unlock(&mappings_mutex);
        
        printk(KERN_INFO "CAM: Removed mapping for PID=%d TID=%d\n", pid, tid);
        return 0;
    }
    
    mutex_unlock(&mappings_mutex);
    return -ENOENT;
}

/* Device file operations */
static int cam_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "CAM: Device opened\n");
    return 0;
}

static int cam_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "CAM: Device closed\n");
    return 0;
}

static long cam_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct affinity_request req;
    struct affinity_info info;
    struct mapping_stats user_stats;
    struct thread_mapping *mapping;
    pid_t pid;
    int ret = 0;
    
    switch (cmd) {
    case CAM_SET_AFFINITY:
        if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
            return -EFAULT;
        }
        
        if (req.cpu_mask == 0) {
            return -EINVAL;
        }
        
        ret = add_mapping(&req);
        break;
        
    case CAM_GET_AFFINITY:
        if (copy_from_user(&info, (void __user *)arg, sizeof(info))) {
            return -EFAULT;
        }
        
        mutex_lock(&mappings_mutex);
        mapping = find_mapping(info.pid, info.tid);
        if (mapping) {
            info.current_mask = mapping->cpu_mask;
            info.requested_mask = mapping->cpu_mask;
            info.policy = mapping->policy;
            info.status = 0;
        } else {
            info.status = -ENOENT;
        }
        mutex_unlock(&mappings_mutex);
        
        if (copy_to_user((void __user *)arg, &info, sizeof(info))) {
            return -EFAULT;
        }
        break;
        
    case CAM_CLEAR_MAPPING:
        if (copy_from_user(&pid, (void __user *)arg, sizeof(pid))) {
            return -EFAULT;
        }
        
        ret = remove_mapping(pid, 0);
        break;
        
    case CAM_GET_STATS:
        user_stats = stats;
        if (copy_to_user((void __user *)arg, &user_stats, sizeof(user_stats))) {
            return -EFAULT;
        }
        break;
        
    default:
        return -ENOTTY;
    }
    
    return ret;
}

static ssize_t cam_read(struct file *file, char __user *buffer, size_t len, loff_t *offset)
{
    char info_buffer[512];
    int info_len;
    
    if (*offset > 0) {
        return 0;  /* EOF */
    }
    
    info_len = snprintf(info_buffer, sizeof(info_buffer),
                       "CPU Affinity Mapper Statistics:\n"
                       "Total Mappings: %d\n"
                       "Active Mappings: %d\n"
                       "Failed Mappings: %d\n"
                       "Total Switches: %lu\n"
                       "Available CPUs: %d\n",
                       stats.total_mappings,
                       stats.active_mappings,
                       stats.failed_mappings,
                       stats.total_switches,
                       num_online_cpus());
    
    if (len < info_len) {
        return -EINVAL;
    }
    
    if (copy_to_user(buffer, info_buffer, info_len)) {
        return -EFAULT;
    }
    
    *offset += info_len;
    return info_len;
}

static ssize_t cam_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset)
{
    char cmd_buffer[64];
    struct affinity_request req;
    int ret;
    
    if (len >= sizeof(cmd_buffer)) {
        return -EINVAL;
    }
    
    if (copy_from_user(cmd_buffer, buffer, len)) {
        return -EFAULT;
    }
    
    cmd_buffer[len] = '\0';
    
    /* Simple command parsing: "pid:tid:mask" */
    ret = sscanf(cmd_buffer, "%d:%d:%lx", &req.pid, &req.tid, &req.cpu_mask);
    if (ret == 3) {
        req.policy = 0;  /* Strict by default */
        ret = add_mapping(&req);
        if (ret == 0) {
            return len;
        }
        return ret;
    }
    
    return -EINVAL;
}

/* Proc file operations */
static int cam_proc_show(struct seq_file *m, void *v)
{
    struct thread_mapping *mapping;
    
    seq_printf(m, "CPU Affinity Mapper - Active Mappings:\n");
    seq_printf(m, "%-8s %-8s %-16s %-8s %-12s\n", 
               "PID", "TID", "CPU_MASK", "POLICY", "SWITCHES");
    
    mutex_lock(&mappings_mutex);
    list_for_each_entry(mapping, &mappings_list, list) {
        seq_printf(m, "%-8d %-8d 0x%-14lx %-8d %-12lu\n",
                   mapping->pid,
                   mapping->tid,
                   mapping->cpu_mask,
                   mapping->policy,
                   mapping->switches);
    }
    mutex_unlock(&mappings_mutex);
    
    return 0;
}

static int cam_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, cam_proc_show, NULL);
}

static const struct proc_ops cam_proc_ops = {
    .proc_open = cam_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

/* Module initialization */
static int __init cam_init(void)
{
    int ret;
    
    printk(KERN_INFO "CAM: Initializing CPU Affinity Mapper driver\n");
    
    /* Allocate device number */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "CAM: Failed to allocate device number\n");
        return ret;
    }
    
    /* Create device class */
    cam_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(cam_class)) {
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(cam_class);
    }
    
    /* Create device */
    cam_device = device_create(cam_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(cam_device)) {
        class_destroy(cam_class);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(cam_device);
    }
    
    /* Initialize character device */
    cdev_init(&cam_cdev, &fops);
    ret = cdev_add(&cam_cdev, dev_num, 1);
    if (ret < 0) {
        device_destroy(cam_class, dev_num);
        class_destroy(cam_class);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }
    
    /* Create proc entry */
    proc_entry = proc_create("cpu_affinity_mapper", 0644, NULL, &cam_proc_ops);
    if (!proc_entry) {
        printk(KERN_WARNING "CAM: Failed to create proc entry\n");
    }
    
    printk(KERN_INFO "CAM: Driver initialized successfully (Major: %d)\n", MAJOR(dev_num));
    return 0;
}

/* Module cleanup */
static void __exit cam_exit(void)
{
    struct thread_mapping *mapping, *tmp;
    
    printk(KERN_INFO "CAM: Cleaning up CPU Affinity Mapper driver\n");
    
    /* Remove proc entry */
    if (proc_entry) {
        proc_remove(proc_entry);
    }
    
    /* Clean up mappings */
    mutex_lock(&mappings_mutex);
    list_for_each_entry_safe(mapping, tmp, &mappings_list, list) {
        list_del(&mapping->list);
        kfree(mapping);
    }
    mutex_unlock(&mappings_mutex);
    
    /* Clean up device */
    cdev_del(&cam_cdev);
    device_destroy(cam_class, dev_num);
    class_destroy(cam_class);
    unregister_chrdev_region(dev_num, 1);
    
    printk(KERN_INFO "CAM: Driver cleanup completed\n");
}

module_init(cam_init);
module_exit(cam_exit);
