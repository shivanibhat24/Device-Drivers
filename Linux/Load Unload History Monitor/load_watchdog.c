/**
 * loadwatchdog.c - Kernel Module Load/Unload Monitoring Device Driver
 * 
 * This driver creates a device file /dev/loadwatchdog which monitors
 * kernel module load/unload operations and maintains verification hashes.
 * 
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <linux/kmod.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>

#define DEVICE_NAME "loadwatchdog"
#define CLASS_NAME "loadwatch"
#define HASH_ALG "sha256"
#define HASH_SIZE 32  /* SHA-256 hash size in bytes */
#define MAX_MODULE_NAME 64
#define MAX_HISTORY_ENTRIES 100
#define LOG_BUFFER_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("Kernel Module Load/Unload Monitoring Device Driver");
MODULE_VERSION("1.0");

/* Module parameters */
static int debug_level = 1;
module_param(debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Debug level (0=off, 1=normal, 2=verbose)");

static int history_size = MAX_HISTORY_ENTRIES;
module_param(history_size, int, 0644);
MODULE_PARM_DESC(history_size, "Maximum number of history entries to keep");

/* Type definitions */
enum module_operation {
    MODULE_LOADED,
    MODULE_UNLOADED
};

struct module_event {
    char name[MAX_MODULE_NAME];
    enum module_operation op;
    u8 hash[HASH_SIZE];
    unsigned long timestamp;
    struct list_head list;
};

/* Global variables */
static int major_number;
static struct class *watchdog_class = NULL;
static struct device *watchdog_device = NULL;
static char *log_buffer;
static size_t log_buffer_pos = 0;
static struct mutex log_mutex;
static struct list_head event_list;
static int event_count = 0;
static struct mutex event_mutex;
static struct task_struct *monitor_thread = NULL;
static bool monitor_running = false;

/* Forward declarations */
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

/* File operations structure */
static struct file_operations fops = {
    .open = device_open,
    .read = device_read,
    .write = device_write,
    .release = device_release,
};

/**
 * Calculate hash for a module
 * @name: Name of the module
 * @hash: Output buffer for hash (must be at least HASH_SIZE bytes)
 * @return: 0 on success, negative error code on failure
 */
static int calculate_module_hash(const char *name, u8 *hash)
{
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    struct module *mod;
    int err = 0;
    
    if (!name || !hash)
        return -EINVAL;
    
    /* Find the module by name */
    mutex_lock(&module_mutex);
    mod = find_module(name);
    if (!mod) {
        mutex_unlock(&module_mutex);
        if (debug_level > 0)
            printk(KERN_WARNING "loadwatchdog: Module %s not found\n", name);
        return -ENOENT;
    }
    
    /* Initialize crypto */
    tfm = crypto_alloc_shash(HASH_ALG, 0, 0);
    if (IS_ERR(tfm)) {
        err = PTR_ERR(tfm);
        mutex_unlock(&module_mutex);
        printk(KERN_ERR "loadwatchdog: Failed to allocate %s crypto\n", HASH_ALG);
        return err;
    }
    
    desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (!desc) {
        crypto_free_shash(tfm);
        mutex_unlock(&module_mutex);
        return -ENOMEM;
    }
    
    desc->tfm = tfm;
    
    /* Calculate hash from module core and init sections */
    err = crypto_shash_init(desc);
    if (err) {
        printk(KERN_ERR "loadwatchdog: Failed to initialize hash\n");
        goto out;
    }
    
    if (mod->core_layout.base && mod->core_layout.size) {
        err = crypto_shash_update(desc, mod->core_layout.base, mod->core_layout.size);
        if (err) {
            printk(KERN_ERR "loadwatchdog: Failed to update hash with core section\n");
            goto out;
        }
    }
    
    if (mod->init_layout.base && mod->init_layout.size) {
        err = crypto_shash_update(desc, mod->init_layout.base, mod->init_layout.size);
        if (err) {
            printk(KERN_ERR "loadwatchdog: Failed to update hash with init section\n");
            goto out;
        }
    }
    
    err = crypto_shash_final(desc, hash);
    if (err)
        printk(KERN_ERR "loadwatchdog: Failed to finalize hash\n");

out:
    kfree(desc);
    crypto_free_shash(tfm);
    mutex_unlock(&module_mutex);
    return err;
}

/**
 * Add event to history
 * @name: Module name
 * @op: Operation (load/unload)
 * @return: 0 on success, negative error code on failure
 */
static int add_module_event(const char *name, enum module_operation op)
{
    struct module_event *event;
    int err;
    
    if (!name)
        return -EINVAL;
    
    event = kmalloc(sizeof(struct module_event), GFP_KERNEL);
    if (!event)
        return -ENOMEM;
    
    strncpy(event->name, name, MAX_MODULE_NAME - 1);
    event->name[MAX_MODULE_NAME - 1] = '\0';
    event->op = op;
    event->timestamp = jiffies;
    
    /* Calculate hash if module is being loaded */
    if (op == MODULE_LOADED) {
        err = calculate_module_hash(name, event->hash);
        if (err) {
            if (debug_level > 0)
                printk(KERN_WARNING "loadwatchdog: Failed to calculate hash for %s: %d\n", 
                       name, err);
            /* Still add the event but with zeroed hash */
            memset(event->hash, 0, HASH_SIZE);
        }
    } else {
        /* For unloaded modules, we can't calculate hash, so use zeroes */
        memset(event->hash, 0, HASH_SIZE);
    }
    
    mutex_lock(&event_mutex);
    
    /* Add to list */
    list_add(&event->list, &event_list);
    event_count++;
    
    /* Remove oldest entries if we've reached the limit */
    while (event_count > history_size && !list_empty(&event_list)) {
        struct module_event *oldest;
        oldest = list_last_entry(&event_list, struct module_event, list);
        list_del(&oldest->list);
        kfree(oldest);
        event_count--;
    }
    
    mutex_unlock(&event_mutex);
    
    return 0;
}

/**
 * Log message to buffer
 * @fmt: Format string
 * @...: Format arguments
 */
static void log_message(const char *fmt, ...)
{
    va_list args;
    int len;
    
    mutex_lock(&log_mutex);
    
    va_start(args, fmt);
    len = vsnprintf(log_buffer + log_buffer_pos, 
                   LOG_BUFFER_SIZE - log_buffer_pos, 
                   fmt, args);
    va_end(args);
    
    if (len > 0 && log_buffer_pos + len < LOG_BUFFER_SIZE) {
        log_buffer_pos += len;
    }
    
    mutex_unlock(&log_mutex);
}

/**
 * Module notification callback
 * @nb: Notifier block
 * @event: Event type
 * @data: Module pointer
 * @return: NOTIFY_DONE
 */
static int module_notify(struct notifier_block *nb, unsigned long event, void *data)
{
    struct module *mod = data;
    
    if (!mod || !mod->name)
        return NOTIFY_DONE;
    
    switch (event) {
    case MODULE_STATE_COMING:
        if (debug_level > 1)
            printk(KERN_INFO "loadwatchdog: Module %s loading\n", mod->name);
        add_module_event(mod->name, MODULE_LOADED);
        log_message("Module loaded: %s\n", mod->name);
        break;
        
    case MODULE_STATE_GOING:
        if (debug_level > 1)
            printk(KERN_INFO "loadwatchdog: Module %s unloading\n", mod->name);
        add_module_event(mod->name, MODULE_UNLOADED);
        log_message("Module unloaded: %s\n", mod->name);
        break;
    }
    
    return NOTIFY_DONE;
}

/* Notifier block */
static struct notifier_block nb = {
    .notifier_call = module_notify
};

/**
 * Monitor thread function
 * @data: Unused
 * @return: 0
 */
static int monitor_func(void *data)
{
    struct module *mod;
    
    if (debug_level > 0)
        printk(KERN_INFO "loadwatchdog: Monitor thread started\n");
    
    while (!kthread_should_stop()) {
        /* Verify existing modules periodically */
        mutex_lock(&module_mutex);
        list_for_each_entry(mod, &THIS_MODULE->list, list) {
            u8 current_hash[HASH_SIZE];
            int err;
            bool hash_changed = false;
            
            if (mod == THIS_MODULE)
                continue;
            
            err = calculate_module_hash(mod->name, current_hash);
            if (err)
                continue;
            
            /* Check if hash changed */
            mutex_lock(&event_mutex);
            if (!list_empty(&event_list)) {
                struct module_event *event;
                list_for_each_entry(event, &event_list, list) {
                    if (strcmp(event->name, mod->name) == 0 && 
                        event->op == MODULE_LOADED) {
                        /* If we have a non-zero hash and it doesn't match */
                        if (!all_zeros(event->hash, HASH_SIZE) && 
                            memcmp(event->hash, current_hash, HASH_SIZE) != 0) {
                            hash_changed = true;
                        }
                        break;
                    }
                }
            }
            mutex_unlock(&event_mutex);
            
            if (hash_changed) {
                printk(KERN_ALERT "loadwatchdog: WARNING! Module %s hash changed after loading!\n", 
                       mod->name);
                log_message("ALERT: Module %s hash changed after loading!\n", mod->name);
            }
        }
        mutex_unlock(&module_mutex);
        
        /* Sleep for 10 seconds */
        msleep_interruptible(10000);
    }
    
    if (debug_level > 0)
        printk(KERN_INFO "loadwatchdog: Monitor thread stopped\n");
    
    return 0;
}

/**
 * Check if buffer contains all zeros
 * @buf: Buffer to check
 * @size: Size of buffer
 * @return: true if all bytes are zero, false otherwise
 */
static bool all_zeros(const u8 *buf, size_t size)
{
    size_t i;
    
    for (i = 0; i < size; i++) {
        if (buf[i] != 0)
            return false;
    }
    
    return true;
}

/**
 * Format hash as hexadecimal string
 * @hash: Hash bytes
 * @hexstr: Output buffer (must be at least 2*HASH_SIZE+1 bytes)
 */
static void hash_to_hex(const u8 *hash, char *hexstr)
{
    int i;
    
    for (i = 0; i < HASH_SIZE; i++)
        sprintf(hexstr + (i * 2), "%02x", hash[i]);
    
    hexstr[HASH_SIZE * 2] = '\0';
}

/**
 * Device open function
 */
static int device_open(struct inode *inode, struct file *file)
{
    /* Reset log position for this read session */
    mutex_lock(&log_mutex);
    file->private_data = (void*)log_buffer_pos;
    mutex_unlock(&log_mutex);
    
    return 0;
}

/**
 * Device release function
 */
static int device_release(struct inode *inode, struct file *file)
{
    return 0;
}

/**
 * Device read function
 */
static ssize_t device_read(struct file *filp, char *buffer, size_t len, loff_t *offset)
{
    size_t bytes_read = 0;
    size_t start_pos = (size_t)filp->private_data;
    size_t curr_pos;
    
    mutex_lock(&log_mutex);
    curr_pos = log_buffer_pos;
    
    /* No new data */
    if (start_pos >= curr_pos) {
        mutex_unlock(&log_mutex);
        return 0;
    }
    
    /* Calculate how much to read */
    bytes_read = curr_pos - start_pos;
    if (bytes_read > len)
        bytes_read = len;
    
    /* Copy to user */
    if (copy_to_user(buffer, log_buffer + start_pos, bytes_read)) {
        mutex_unlock(&log_mutex);
        return -EFAULT;
    }
    
    /* Update file position */
    filp->private_data = (void*)(start_pos + bytes_read);
    
    mutex_unlock(&log_mutex);
    return bytes_read;
}

/**
 * Process user commands
 * @cmd: Command string
 * @len: Length of command
 * @return: 0 on success, negative error code on failure
 */
static int process_command(const char *cmd, size_t len)
{
    char command[32];
    char arg[MAX_MODULE_NAME];
    int ret = 0;
    
    /* Simple command parsing */
    if (sscanf(cmd, "%31s %63s", command, arg) >= 1) {
        if (strncmp(command, "list", 4) == 0) {
            struct module_event *event;
            char hash_str[HASH_SIZE * 2 + 1];
            
            log_message("Module history:\n");
            
            mutex_lock(&event_mutex);
            list_for_each_entry(event, &event_list, list) {
                hash_to_hex(event->hash, hash_str);
                log_message("  %s: %s at %lu, hash: %s\n", 
                           event->name,
                           event->op == MODULE_LOADED ? "loaded" : "unloaded",
                           event->timestamp,
                           event->op == MODULE_LOADED ? hash_str : "N/A");
            }
            mutex_unlock(&event_mutex);
            
            ret = 0;
        } else if (strncmp(command, "verify", 6) == 0) {
            if (arg[0]) {
                u8 hash[HASH_SIZE];
                char hash_str[HASH_SIZE * 2 + 1];
                ret = calculate_module_hash(arg, hash);
                
                if (ret == 0) {
                    hash_to_hex(hash, hash_str);
                    log_message("Module %s current hash: %s\n", arg, hash_str);
                } else {
                    log_message("Failed to verify module %s: %d\n", arg, ret);
                }
            } else {
                log_message("Usage: verify <module_name>\n");
            }
        } else if (strncmp(command, "clear", 5) == 0) {
            mutex_lock(&event_mutex);
            while (!list_empty(&event_list)) {
                struct module_event *event;
                event = list_first_entry(&event_list, struct module_event, list);
                list_del(&event->list);
                kfree(event);
            }
            event_count = 0;
            mutex_unlock(&event_mutex);
            
            log_message("Module history cleared\n");
            ret = 0;
        } else if (strncmp(command, "help", 4) == 0) {
            log_message("Available commands:\n");
            log_message("  list - List module load/unload history\n");
            log_message("  verify <module> - Calculate hash for specified module\n");
            log_message("  clear - Clear module history\n");
            log_message("  help - Show this help\n");
            ret = 0;
        } else {
            log_message("Unknown command: %s (try 'help')\n", command);
            ret = -EINVAL;
        }
    } else {
        log_message("Invalid command format\n");
        ret = -EINVAL;
    }
    
    return ret;
}

/**
 * Device write function
 */
static ssize_t device_write(struct file *filp, const char *buffer, size_t len, loff_t *offset)
{
    char *cmd_buffer;
    int ret;
    
    /* Allocate temporary buffer for command */
    cmd_buffer = kmalloc(len + 1, GFP_KERNEL);
    if (!cmd_buffer)
        return -ENOMEM;
    
    /* Copy command from user space */
    if (copy_from_user(cmd_buffer, buffer, len)) {
        kfree(cmd_buffer);
        return -EFAULT;
    }
    
    /* Ensure null termination */
    cmd_buffer[len] = '\0';
    
    /* Process command */
    ret = process_command(cmd_buffer, len);
    
    kfree(cmd_buffer);
    
    if (ret < 0)
        return ret;
    
    return len;
}

/**
 * Module initialization
 */
static int __init loadwatchdog_init(void)
{
    /* Initialize data structures */
    INIT_LIST_HEAD(&event_list);
    mutex_init(&event_mutex);
    mutex_init(&log_mutex);
    
    /* Allocate log buffer */
    log_buffer = kmalloc(LOG_BUFFER_SIZE, GFP_KERNEL);
    if (!log_buffer) {
        printk(KERN_ERR "loadwatchdog: Failed to allocate log buffer\n");
        return -ENOMEM;
    }
    memset(log_buffer, 0, LOG_BUFFER_SIZE);
    
    /* Register device */
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ERR "loadwatchdog: Failed to register device: %d\n", major_number);
        kfree(log_buffer);
        return major_number;
    }
    
    /* Create device class */
    watchdog_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(watchdog_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        kfree(log_buffer);
        printk(KERN_ERR "loadwatchdog: Failed to create device class\n");
        return PTR_ERR(watchdog_class);
    }
    
    /* Create device */
    watchdog_device = device_create(watchdog_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(watchdog_device)) {
        class_destroy(watchdog_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        kfree(log_buffer);
        printk(KERN_ERR "loadwatchdog: Failed to create device\n");
        return PTR_ERR(watchdog_device);
    }
    
    /* Register for module notifications */
    register_module_notifier(&nb);
    
    /* Create monitoring thread */
    monitor_thread = kthread_run(monitor_func, NULL, "watchdog_monitor");
    if (IS_ERR(monitor_thread)) {
        printk(KERN_ERR "loadwatchdog: Failed to create monitor thread\n");
        device_destroy(watchdog_class, MKDEV(major_number, 0));
        class_destroy(watchdog_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        kfree(log_buffer);
        return PTR_ERR(monitor_thread);
    }
    monitor_running = true;
    
    /* Initial log message */
    log_message("loadwatchdog initialized, tracking module loads/unloads\n");
    log_message("Use 'help' command for available commands\n");
    
    printk(KERN_INFO "loadwatchdog: Initialized (/dev/%s)\n", DEVICE_NAME);
    return 0;
}

/**
 * Module cleanup
 */
static void __exit loadwatchdog_exit(void)
{
    struct module_event *event, *tmp;
    
    /* Stop monitor thread */
    if (monitor_running && monitor_thread) {
        kthread_stop(monitor_thread);
        monitor_running = false;
    }
    
    /* Unregister from module notifications */
    unregister_module_notifier(&nb);
    
    /* Free event list */
    mutex_lock(&event_mutex);
    list_for_each_entry_safe(event, tmp, &event_list, list) {
        list_del(&event->list);
        kfree(event);
    }
    mutex_unlock(&event_mutex);
    
    /* Destroy device */
    device_destroy(watchdog_class, MKDEV(major_number, 0));
    class_destroy(watchdog_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    
    /* Free log buffer */
    kfree(log_buffer);
    
    printk(KERN_INFO "loadwatchdog: Exited\n");
}

module_init(loadwatchdog_init);
module_exit(loadwatchdog_exit);
