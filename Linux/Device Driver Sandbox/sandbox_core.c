/*
 * sandbox_core.c - Linux Kernel Driver Sandbox Core Module
 * 
 * Provides a secure, isolated environment for testing kernel drivers
 * without risking system stability.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/relay.h>
#include <linux/debugfs.h>
#include <linux/kprobes.h>
#include <linux/random.h>
#include <linux/version.h>

#define SANDBOX_VERSION "1.0.0"
#define SANDBOX_MAX_DRIVERS 16
#define SANDBOX_LOG_BUFFER_SIZE (64 * 1024)
#define SANDBOX_IRQ_BASE 200

/* Sandbox driver metadata */
struct sandbox_driver_info {
    struct module *mod;
    struct device *dev;
    char name[64];
    uid_t owner_uid;
    bool active;
    unsigned long load_time;
    atomic_t ref_count;
    struct list_head list;
};

/* Sandbox context */
struct sandbox_context {
    struct mutex lock;
    struct list_head drivers;
    int driver_count;
    struct relay_chan *log_chan;
    struct dentry *debugfs_root;
    struct proc_dir_entry *proc_root;
    struct workqueue_struct *irq_wq;
    struct timer_list irq_timer;
    bool irq_simulation_enabled;
};

/* Global sandbox context */
static struct sandbox_context sandbox_ctx;

/* Sandbox bus type */
static struct bus_type sandbox_bus_type = {
    .name = "sandbox",
};

/* Sandbox device class */
static struct class *sandbox_class;

/* Logging functions */
static void sandbox_log(const char *fmt, ...)
{
    va_list args;
    char buffer[256];
    int len;
    
    va_start(args, fmt);
    len = vscnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    /* Log to kernel ring buffer */
    printk(KERN_INFO "sandbox: %s", buffer);
    
    /* Log to relay channel if available */
    if (sandbox_ctx.log_chan) {
        relay_write(sandbox_ctx.log_chan, buffer, len);
    }
}

/* Sandbox driver registration */
int sandbox_register_driver(struct platform_driver *drv, struct module *mod)
{
    struct sandbox_driver_info *info;
    struct device *dev;
    int ret = 0;
    
    if (!drv || !mod) {
        return -EINVAL;
    }
    
    mutex_lock(&sandbox_ctx.lock);
    
    if (sandbox_ctx.driver_count >= SANDBOX_MAX_DRIVERS) {
        ret = -ENOSPC;
        goto out;
    }
    
    info = kzalloc(sizeof(*info), GFP_KERNEL);
    if (!info) {
        ret = -ENOMEM;
        goto out;
    }
    
    /* Create sandbox device */
    dev = device_create(sandbox_class, NULL, MKDEV(0, 0), NULL, 
                       "sandbox_%s", drv->driver.name);
    if (IS_ERR(dev)) {
        ret = PTR_ERR(dev);
        kfree(info);
        goto out;
    }
    
    /* Initialize driver info */
    info->mod = mod;
    info->dev = dev;
    strncpy(info->name, drv->driver.name, sizeof(info->name) - 1);
    info->owner_uid = current_uid().val;
    info->active = true;
    info->load_time = jiffies;
    atomic_set(&info->ref_count, 1);
    
    /* Add to sandbox context */
    list_add_tail(&info->list, &sandbox_ctx.drivers);
    sandbox_ctx.driver_count++;
    
    sandbox_log("Driver '%s' registered (UID: %u)\n", 
                info->name, info->owner_uid);
    
out:
    mutex_unlock(&sandbox_ctx.lock);
    return ret;
}
EXPORT_SYMBOL(sandbox_register_driver);

/* Sandbox driver unregistration */
void sandbox_unregister_driver(const char *name)
{
    struct sandbox_driver_info *info, *tmp;
    
    mutex_lock(&sandbox_ctx.lock);
    
    list_for_each_entry_safe(info, tmp, &sandbox_ctx.drivers, list) {
        if (strcmp(info->name, name) == 0) {
            info->active = false;
            if (atomic_dec_and_test(&info->ref_count)) {
                device_destroy(sandbox_class, info->dev->devt);
                list_del(&info->list);
                sandbox_ctx.driver_count--;
                sandbox_log("Driver '%s' unregistered\n", name);
                kfree(info);
            }
            break;
        }
    }
    
    mutex_unlock(&sandbox_ctx.lock);
}
EXPORT_SYMBOL(sandbox_unregister_driver);

/* File operations interceptor */
static long sandbox_ioctl_interceptor(struct file *file, unsigned int cmd, 
                                     unsigned long arg)
{
    struct sandbox_driver_info *info;
    long ret;
    
    /* Find the driver info */
    mutex_lock(&sandbox_ctx.lock);
    list_for_each_entry(info, &sandbox_ctx.drivers, list) {
        if (file->f_inode->i_rdev == info->dev->devt) {
            sandbox_log("IOCTL intercept: %s cmd=0x%x arg=0x%lx\n",
                       info->name, cmd, arg);
            break;
        }
    }
    mutex_unlock(&sandbox_ctx.lock);
    
    /* Call original ioctl if it exists */
    if (file->f_op && file->f_op->unlocked_ioctl) {
        ret = file->f_op->unlocked_ioctl(file, cmd, arg);
    } else {
        ret = -ENOTTY;
    }
    
    return ret;
}

/* IRQ simulation work function */
static void sandbox_irq_work(struct work_struct *work)
{
    struct sandbox_driver_info *info;
    int fake_irq;
    
    if (!sandbox_ctx.irq_simulation_enabled) {
        return;
    }
    
    /* Generate fake IRQ number */
    get_random_bytes(&fake_irq, sizeof(fake_irq));
    fake_irq = SANDBOX_IRQ_BASE + (fake_irq % 16);
    
    mutex_lock(&sandbox_ctx.lock);
    list_for_each_entry(info, &sandbox_ctx.drivers, list) {
        if (info->active) {
            sandbox_log("Simulated IRQ %d for driver '%s'\n", 
                       fake_irq, info->name);
            /* Here we would trigger the driver's interrupt handler */
        }
    }
    mutex_unlock(&sandbox_ctx.lock);
}

static DECLARE_WORK(sandbox_irq_work_struct, sandbox_irq_work);

/* IRQ timer callback */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0)
static void sandbox_irq_timer_callback(struct timer_list *timer)
#else
static void sandbox_irq_timer_callback(unsigned long data)
#endif
{
    if (sandbox_ctx.irq_simulation_enabled) {
        queue_work(sandbox_ctx.irq_wq, &sandbox_irq_work_struct);
        mod_timer(&sandbox_ctx.irq_timer, jiffies + HZ);
    }
}

/* Proc filesystem interface */
static int sandbox_proc_show(struct seq_file *m, void *v)
{
    struct sandbox_driver_info *info;
    
    seq_printf(m, "Sandbox Version: %s\n", SANDBOX_VERSION);
    seq_printf(m, "Active Drivers: %d/%d\n", 
               sandbox_ctx.driver_count, SANDBOX_MAX_DRIVERS);
    seq_printf(m, "IRQ Simulation: %s\n", 
               sandbox_ctx.irq_simulation_enabled ? "ON" : "OFF");
    seq_printf(m, "\nRegistered Drivers:\n");
    seq_printf(m, "%-20s %-8s %-12s %s\n", 
               "Name", "UID", "Load Time", "Status");
    seq_printf(m, "%-20s %-8s %-12s %s\n", 
               "----", "---", "---------", "------");
    
    mutex_lock(&sandbox_ctx.lock);
    list_for_each_entry(info, &sandbox_ctx.drivers, list) {
        seq_printf(m, "%-20s %-8u %-12lu %s\n",
                   info->name, info->owner_uid, info->load_time,
                   info->active ? "ACTIVE" : "INACTIVE");
    }
    mutex_unlock(&sandbox_ctx.lock);
    
    return 0;
}

static int sandbox_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, sandbox_proc_show, NULL);
}

static const struct proc_ops sandbox_proc_ops = {
    .proc_open = sandbox_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

/* IRQ control proc interface */
static ssize_t sandbox_irqctl_write(struct file *file, const char __user *buffer,
                                   size_t count, loff_t *pos)
{
    char cmd[16];
    
    if (count > sizeof(cmd) - 1) {
        return -EINVAL;
    }
    
    if (copy_from_user(cmd, buffer, count)) {
        return -EFAULT;
    }
    
    cmd[count] = '\0';
    
    if (strncmp(cmd, "enable", 6) == 0) {
        sandbox_ctx.irq_simulation_enabled = true;
        mod_timer(&sandbox_ctx.irq_timer, jiffies + HZ);
        sandbox_log("IRQ simulation enabled\n");
    } else if (strncmp(cmd, "disable", 7) == 0) {
        sandbox_ctx.irq_simulation_enabled = false;
        del_timer_sync(&sandbox_ctx.irq_timer);
        sandbox_log("IRQ simulation disabled\n");
    }
    
    return count;
}

static const struct proc_ops sandbox_irqctl_ops = {
    .proc_write = sandbox_irqctl_write,
};

/* Relay filesystem subbuffer callback */
static int sandbox_subbuf_start(struct rchan_buf *buf, void *subbuf,
                               void *prev_subbuf, size_t prev_padding)
{
    return 1; /* Always start new subbuffer */
}

static struct rchan_callbacks sandbox_relay_callbacks = {
    .subbuf_start = sandbox_subbuf_start,
};

/* Module initialization */
static int __init sandbox_init(void)
{
    int ret;
    
    printk(KERN_INFO "sandbox: Linux Kernel Driver Sandbox v%s\n", 
           SANDBOX_VERSION);
    
    /* Initialize sandbox context */
    mutex_init(&sandbox_ctx.lock);
    INIT_LIST_HEAD(&sandbox_ctx.drivers);
    sandbox_ctx.driver_count = 0;
    sandbox_ctx.irq_simulation_enabled = false;
    
    /* Register sandbox bus */
    ret = bus_register(&sandbox_bus_type);
    if (ret) {
        printk(KERN_ERR "sandbox: Failed to register bus type\n");
        return ret;
    }
    
    /* Create sandbox device class */
    sandbox_class = class_create(THIS_MODULE, "sandbox");
    if (IS_ERR(sandbox_class)) {
        ret = PTR_ERR(sandbox_class);
        bus_unregister(&sandbox_bus_type);
        return ret;
    }
    
    /* Create proc filesystem entries */
    sandbox_ctx.proc_root = proc_mkdir("sandbox", NULL);
    if (sandbox_ctx.proc_root) {
        proc_create("status", 0444, sandbox_ctx.proc_root, &sandbox_proc_ops);
        proc_create("irqctl", 0200, sandbox_ctx.proc_root, &sandbox_irqctl_ops);
    }
    
    /* Create debugfs entries */
    sandbox_ctx.debugfs_root = debugfs_create_dir("sandbox", NULL);
    
    /* Create relay channel for logging */
    sandbox_ctx.log_chan = relay_open("sandbox_log", sandbox_ctx.debugfs_root,
                                     SANDBOX_LOG_BUFFER_SIZE, 4,
                                     &sandbox_relay_callbacks, NULL);
    
    /* Create IRQ simulation workqueue */
    sandbox_ctx.irq_wq = create_singlethread_workqueue("sandbox_irq");
    if (!sandbox_ctx.irq_wq) {
        ret = -ENOMEM;
        goto cleanup;
    }
    
    /* Initialize IRQ timer */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0)
    timer_setup(&sandbox_ctx.irq_timer, sandbox_irq_timer_callback, 0);
#else
    setup_timer(&sandbox_ctx.irq_timer, sandbox_irq_timer_callback, 0);
#endif
    
    sandbox_log("Sandbox core module loaded successfully\n");
    return 0;
    
cleanup:
    if (sandbox_ctx.log_chan) {
        relay_close(sandbox_ctx.log_chan);
    }
    debugfs_remove_recursive(sandbox_ctx.debugfs_root);
    proc_remove(sandbox_ctx.proc_root);
    class_destroy(sandbox_class);
    bus_unregister(&sandbox_bus_type);
    return ret;
}

/* Module cleanup */
static void __exit sandbox_exit(void)
{
    struct sandbox_driver_info *info, *tmp;
    
    /* Stop IRQ simulation */
    sandbox_ctx.irq_simulation_enabled = false;
    del_timer_sync(&sandbox_ctx.irq_timer);
    
    /* Destroy workqueue */
    if (sandbox_ctx.irq_wq) {
        destroy_workqueue(sandbox_ctx.irq_wq);
    }
    
    /* Cleanup registered drivers */
    mutex_lock(&sandbox_ctx.lock);
    list_for_each_entry_safe(info, tmp, &sandbox_ctx.drivers, list) {
        device_destroy(sandbox_class, info->dev->devt);
        list_del(&info->list);
        kfree(info);
    }
    mutex_unlock(&sandbox_ctx.lock);
    
    /* Cleanup filesystem interfaces */
    if (sandbox_ctx.log_chan) {
        relay_close(sandbox_ctx.log_chan);
    }
    debugfs_remove_recursive(sandbox_ctx.debugfs_root);
    proc_remove(sandbox_ctx.proc_root);
    
    /* Cleanup device class and bus */
    class_destroy(sandbox_class);
    bus_unregister(&sandbox_bus_type);
    
    sandbox_log("Sandbox core module unloaded\n");
}

module_init(sandbox_init);
module_exit(sandbox_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sandbox Developer");
MODULE_DESCRIPTION("Linux Kernel Driver Sandbox Core Module");
MODULE_VERSION(SANDBOX_VERSION);
