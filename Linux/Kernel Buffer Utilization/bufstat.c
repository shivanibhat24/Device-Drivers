/**
 * bufstat.c - Linux kernel module that creates /dev/bufstat to monitor kernel buffer usage
 *
 * This module creates a character device that reports kernel buffer utilization
 * across devices when read from. It provides information about various kernel
 * buffers including network, block, filesystem and other subsystems.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/vmstat.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm_types.h>
#include <linux/blk-mq.h>
#include <linux/bio.h>
#include <linux/version.h>

#define DEVICE_NAME "bufstat"
#define CLASS_NAME "bufstat"
#define BUFSTAT_BUFFER_SIZE 8192

/* Module parameters */
static int debug_mode = 0;
module_param(debug_mode, int, 0644);
MODULE_PARM_DESC(debug_mode, "Enable debug messages (0=off, 1=on)");

/* Module information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kernel Developer");
MODULE_DESCRIPTION("Kernel Buffer Statistics Driver");
MODULE_VERSION("1.0");

/* Device variables */
static int major_number;
static struct class *bufstat_class = NULL;
static struct device *bufstat_device = NULL;
static struct cdev bufstat_cdev;
static char *bufstat_buffer;

/* Synchronization */
static DEFINE_MUTEX(bufstat_mutex);

/* Debugging macro */
#define BUFSTAT_DEBUG(fmt, ...) \
    do { if (debug_mode) pr_info("bufstat: " fmt, ##__VA_ARGS__); } while (0)

/* Function prototypes */
static int bufstat_open(struct inode *, struct file *);
static int bufstat_release(struct inode *, struct file *);
static ssize_t bufstat_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t bufstat_write(struct file *, const char __user *, size_t, loff_t *);

/* File operations structure */
static struct file_operations bufstat_fops = {
    .owner = THIS_MODULE,
    .open = bufstat_open,
    .release = bufstat_release,
    .read = bufstat_read,
    .write = bufstat_write,
};

/**
 * Get network buffer statistics
 * @param buffer: The buffer to write the stats to
 * @param size: Size of the buffer
 * @return Number of bytes written to the buffer
 */
static int get_network_buffer_stats(char *buffer, size_t size)
{
    int len = 0;
    struct net *net;
    struct net_device *dev;

    len += snprintf(buffer + len, size - len, "=== Network Buffer Statistics ===\n");
    
    /* Count total socket buffers in use */
    len += snprintf(buffer + len, size - len, "Socket buffer allocation: %u\n", 
                   atomic_read(&sk_buff_allocated));

    /* List stats for each network device */
    len += snprintf(buffer + len, size - len, "\nPer-device buffer statistics:\n");
    
    read_lock(&dev_base_lock);
    for_each_net(net) {
        for_each_netdev(net, dev) {
            if (dev->stats.rx_packets || dev->stats.tx_packets) {
                len += snprintf(buffer + len, size - len,
                    "  %s: rx_buffers=%llu tx_buffers=%llu\n",
                    dev->name,
                    dev->stats.rx_packets,
                    dev->stats.tx_packets);
            }
            
            /* Check if we're still within buffer bounds */
            if (len >= size - 200) {
                len += snprintf(buffer + len, size - len, "... (more devices truncated)\n");
                break;
            }
        }
    }
    read_unlock(&dev_base_lock);

    return len;
}

/**
 * Get block device buffer statistics
 * @param buffer: The buffer to write the stats to
 * @param size: Size of the buffer
 * @return Number of bytes written to the buffer
 */
static int get_block_buffer_stats(char *buffer, size_t size)
{
    int len = 0;
    
    len += snprintf(buffer + len, size - len, "\n=== Block Buffer Statistics ===\n");
    
    /* Get block info from /proc/meminfo style stats */
    len += snprintf(buffer + len, size - len, "Buffers: %lu kB\n", 
                   global_node_page_state(NR_FILE_PAGES) * 4);
    
    /* Page cache information */
    len += snprintf(buffer + len, size - len, "Cached: %lu kB\n", 
                   global_node_page_state(NR_FILE_PAGES) * 4 -
                   global_node_page_state(NR_SHMEM) * 4);
    
    len += snprintf(buffer + len, size - len, "Dirty pages: %lu kB\n",
                   global_node_page_state(NR_FILE_DIRTY) * 4);
    
    len += snprintf(buffer + len, size - len, "Writeback pages: %lu kB\n",
                   global_node_page_state(NR_WRITEBACK) * 4);
    
    return len;
}

/**
 * Get memory management buffer statistics
 * @param buffer: The buffer to write the stats to
 * @param size: Size of the buffer
 * @return Number of bytes written to the buffer
 */
static int get_mm_buffer_stats(char *buffer, size_t size)
{
    int len = 0;
    
    len += snprintf(buffer + len, size - len, "\n=== Memory Management Statistics ===\n");
    
    /* Slab info */
    len += snprintf(buffer + len, size - len, "Slab memory: %lu kB\n", 
                   global_node_page_state(NR_SLAB_UNRECLAIMABLE) * 4 +
                   global_node_page_state(NR_SLAB_RECLAIMABLE) * 4);
                   
    len += snprintf(buffer + len, size - len, "  Reclaimable: %lu kB\n", 
                   global_node_page_state(NR_SLAB_RECLAIMABLE) * 4);
                   
    len += snprintf(buffer + len, size - len, "  Unreclaimable: %lu kB\n", 
                   global_node_page_state(NR_SLAB_UNRECLAIMABLE) * 4);
    
    /* Page tables */
    len += snprintf(buffer + len, size - len, "PageTables: %lu kB\n", 
                   global_node_page_state(NR_PAGETABLE) * 4);
    
    return len;
}

/**
 * Generate the complete buffer statistics report
 * @param buffer: Buffer to write the report into
 * @param size: Size of the buffer
 * @return Number of bytes written
 */
static int generate_buffer_stats(char *buffer, size_t size)
{
    int len = 0;
    struct timespec64 ts;
    
    /* Header information with timestamp */
    ktime_get_real_ts64(&ts);
    len += snprintf(buffer, size, "Kernel Buffer Statistics Report\n");
    len += snprintf(buffer + len, size - len, "Generated: %lld.%ld\n\n", 
                   (long long)ts.tv_sec, ts.tv_nsec / 1000000);
    
    /* Gather stats from different subsystems */
    len += get_network_buffer_stats(buffer + len, size - len);
    
    if (len < size - 100)
        len += get_block_buffer_stats(buffer + len, size - len);
    
    if (len < size - 100)
        len += get_mm_buffer_stats(buffer + len, size - len);
    
    /* Summary section */
    if (len < size - 100) {
        len += snprintf(buffer + len, size - len, 
                       "\n=== Overall Buffer Usage Summary ===\n");
        len += snprintf(buffer + len, size - len, 
                       "Total memory: %lu kB\n", totalram_pages() * 4);
        len += snprintf(buffer + len, size - len, 
                       "Free memory: %lu kB\n", 
                       global_node_page_state(NR_FREE_PAGES) * 4);
        len += snprintf(buffer + len, size - len, 
                       "Available memory: %lu kB\n", 
                       si_mem_available() * 4);
    }
    
    BUFSTAT_DEBUG("Generated buffer stats report, %d bytes\n", len);
    return len;
}

/**
 * Device open function
 */
static int bufstat_open(struct inode *inodep, struct file *filep)
{
    if (!mutex_trylock(&bufstat_mutex)) {
        BUFSTAT_DEBUG("Device busy, can't open\n");
        return -EBUSY;
    }
    
    BUFSTAT_DEBUG("Device opened\n");
    return 0;
}

/**
 * Device release function
 */
static int bufstat_release(struct inode *inodep, struct file *filep)
{
    mutex_unlock(&bufstat_mutex);
    BUFSTAT_DEBUG("Device closed\n");
    return 0;
}

/**
 * Device read function
 */
static ssize_t bufstat_read(struct file *filep, char __user *buffer, 
                           size_t len, loff_t *offset)
{
    int bytes_read = 0;
    int stats_size;
    
    /* If we're at EOF, reset offset and return 0 */
    if (*offset > 0)
        return 0;
    
    /* Generate fresh stats every time */
    stats_size = generate_buffer_stats(bufstat_buffer, BUFSTAT_BUFFER_SIZE);
    
    /* Copy data to user space */
    if (stats_size < len)
        bytes_read = stats_size;
    else
        bytes_read = len;
    
    if (copy_to_user(buffer, bufstat_buffer, bytes_read)) {
        BUFSTAT_DEBUG("Failed to copy data to user\n");
        return -EFAULT;
    }
    
    *offset += bytes_read;
    return bytes_read;
}

/**
 * Device write function (primarily used for control commands)
 */
static ssize_t bufstat_write(struct file *filep, const char __user *buffer, 
                            size_t len, loff_t *offset)
{
    char cmd[32];
    size_t cmd_len = min(len, sizeof(cmd) - 1);
    
    if (copy_from_user(cmd, buffer, cmd_len)) {
        BUFSTAT_DEBUG("Failed to copy command from user\n");
        return -EFAULT;
    }
    
    cmd[cmd_len] = '\0';
    
    /* Simple command handling */
    if (strncmp(cmd, "debug", 5) == 0) {
        debug_mode = 1;
        BUFSTAT_DEBUG("Debug mode enabled via write command\n");
    } else if (strncmp(cmd, "nodebug", 7) == 0) {
        BUFSTAT_DEBUG("Debug mode disabled via write command\n");
        debug_mode = 0;
    } else {
        BUFSTAT_DEBUG("Unknown command: %s\n", cmd);
    }
    
    return len;
}

/**
 * Module initialization function
 */
static int __init bufstat_init(void)
{
    /* Allocate buffer for stats */
    bufstat_buffer = kmalloc(BUFSTAT_BUFFER_SIZE, GFP_KERNEL);
    if (!bufstat_buffer) {
        pr_err("bufstat: Failed to allocate memory\n");
        return -ENOMEM;
    }

    /* Register a character device */
    major_number = register_chrdev(0, DEVICE_NAME, &bufstat_fops);
    if (major_number < 0) {
        pr_err("bufstat: Failed to register a major number\n");
        kfree(bufstat_buffer);
        return major_number;
    }

    /* Register device class */
    bufstat_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(bufstat_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        kfree(bufstat_buffer);
        pr_err("bufstat: Failed to register device class\n");
        return PTR_ERR(bufstat_class);
    }

    /* Create device */
    bufstat_device = device_create(bufstat_class, NULL, MKDEV(major_number, 0), 
                                  NULL, DEVICE_NAME);
    if (IS_ERR(bufstat_device)) {
        class_destroy(bufstat_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        kfree(bufstat_buffer);
        pr_err("bufstat: Failed to create the device\n");
        return PTR_ERR(bufstat_device);
    }

    /* Initialize character device */
    cdev_init(&bufstat_cdev, &bufstat_fops);
    bufstat_cdev.owner = THIS_MODULE;
    
    if (cdev_add(&bufstat_cdev, MKDEV(major_number, 0), 1) < 0) {
        device_destroy(bufstat_class, MKDEV(major_number, 0));
        class_destroy(bufstat_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        kfree(bufstat_buffer);
        pr_err("bufstat: Failed to add cdev\n");
        return -1;
    }

    mutex_init(&bufstat_mutex);
    
    pr_info("bufstat: Device created successfully\n");
    BUFSTAT_DEBUG("Module loaded with debug_mode=%d\n", debug_mode);
    return 0;
}

/**
 * Module exit function
 */
static void __exit bufstat_exit(void)
{
    mutex_destroy(&bufstat_mutex);
    cdev_del(&bufstat_cdev);
    device_destroy(bufstat_class, MKDEV(major_number, 0));
    class_destroy(bufstat_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    kfree(bufstat_buffer);
    pr_info("bufstat: Device removed successfully\n");
}

module_init(bufstat_init);
module_exit(bufstat_exit);
