/*
 * testsuspend.c - Kernel module to simulate suspend/resume cycles
 *
 * This module creates a character device /dev/testsuspend that allows
 * userspace applications to simulate suspend/resume cycles without
 * actually putting the system to sleep.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/pm_wakeup.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/completion.h>

#define DEVICE_NAME "testsuspend"
#define CLASS_NAME "testsuspend"

/* Module parameters */
static int suspend_delay_ms = 1000;
module_param(suspend_delay_ms, int, 0644);
MODULE_PARM_DESC(suspend_delay_ms, "Delay in milliseconds during simulated suspend (default: 1000)");

/* Device variables */
static struct class *testsuspend_class;
static struct cdev testsuspend_cdev;
static dev_t dev_num;
static struct device *testsuspend_device;

/* Suspend simulation variables */
static struct task_struct *suspend_thread;
static struct completion suspend_completion;
static atomic_t is_suspended = ATOMIC_INIT(0);
static DEFINE_MUTEX(suspend_mutex);
static struct wakeup_source *testsuspend_ws;

/* File operations prototypes */
static int testsuspend_open(struct inode *, struct file *);
static int testsuspend_release(struct inode *, struct file *);
static ssize_t testsuspend_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t testsuspend_write(struct file *, const char __user *, size_t, loff_t *);

/* File operations structure */
static struct file_operations testsuspend_fops = {
    .owner = THIS_MODULE,
    .open = testsuspend_open,
    .release = testsuspend_release,
    .read = testsuspend_read,
    .write = testsuspend_write,
};

/* Commands */
#define CMD_START_SUSPEND "suspend"
#define CMD_FORCE_RESUME "resume"
#define CMD_GET_STATUS "status"

/* Thread function to simulate suspend/resume cycle */
static int suspend_thread_fn(void *data)
{
    while (!kthread_should_stop()) {
        /* Wait for a suspend request */
        wait_for_completion(&suspend_completion);
        
        if (kthread_should_stop())
            break;
            
        pr_info("testsuspend: Simulating suspend\n");
        
        /* Notify userspace listeners before suspend */
        kobject_uevent(&testsuspend_device->kobj, KOBJ_CHANGE);
        
        /* Mark as suspended */
        atomic_set(&is_suspended, 1);
        
        /* Simulate suspend with a delay */
        msleep(suspend_delay_ms);
        
        /* Mark as resumed */
        atomic_set(&is_suspended, 0);
        
        pr_info("testsuspend: Simulating resume\n");
        
        /* Notify userspace listeners after resume */
        kobject_uevent(&testsuspend_device->kobj, KOBJ_CHANGE);
    }
    
    return 0;
}

/* Device open function */
static int testsuspend_open(struct inode *inode, struct file *file)
{
    return 0;
}

/* Device release function */
static int testsuspend_release(struct inode *inode, struct file *file)
{
    return 0;
}

/* Device read function - provides current status */
static ssize_t testsuspend_read(struct file *file, char __user *buffer, 
                               size_t length, loff_t *offset)
{
    char status[64];
    int status_len;
    
    if (*offset > 0)
        return 0;
    
    if (atomic_read(&is_suspended))
        status_len = snprintf(status, sizeof(status), "suspended\n");
    else
        status_len = snprintf(status, sizeof(status), "active\n");
    
    if (length < status_len)
        return -EINVAL;
    
    if (copy_to_user(buffer, status, status_len))
        return -EFAULT;
    
    *offset += status_len;
    return status_len;
}

/* Device write function - accepts commands */
static ssize_t testsuspend_write(struct file *file, const char __user *buffer,
                                size_t length, loff_t *offset)
{
    char *cmd_buf;
    int ret = length;
    
    if (length == 0 || length > 64)
        return -EINVAL;
    
    cmd_buf = kmalloc(length + 1, GFP_KERNEL);
    if (!cmd_buf)
        return -ENOMEM;
    
    if (copy_from_user(cmd_buf, buffer, length)) {
        kfree(cmd_buf);
        return -EFAULT;
    }
    
    /* Ensure null termination */
    cmd_buf[length] = '\0';
    
    /* Remove trailing newline if present */
    if (length > 0 && cmd_buf[length-1] == '\n')
        cmd_buf[length-1] = '\0';
    
    /* Process commands */
    if (strncmp(cmd_buf, CMD_START_SUSPEND, strlen(CMD_START_SUSPEND)) == 0) {
        if (mutex_trylock(&suspend_mutex)) {
            if (!atomic_read(&is_suspended)) {
                /* Signal the suspend thread to start a cycle */
                complete(&suspend_completion);
            } else {
                pr_info("testsuspend: Already suspended\n");
            }
            mutex_unlock(&suspend_mutex);
        } else {
            pr_info("testsuspend: Suspend operation already in progress\n");
        }
    } else if (strncmp(cmd_buf, CMD_FORCE_RESUME, strlen(CMD_FORCE_RESUME)) == 0) {
        if (atomic_read(&is_suspended)) {
            atomic_set(&is_suspended, 0);
            pr_info("testsuspend: Forced resume\n");
            /* Notify userspace listeners after forced resume */
            kobject_uevent(&testsuspend_device->kobj, KOBJ_CHANGE);
        }
    } else if (strncmp(cmd_buf, CMD_GET_STATUS, strlen(CMD_GET_STATUS)) == 0) {
        /* Status is handled in read operation, nothing to do here */
    } else {
        pr_info("testsuspend: Unknown command: %s\n", cmd_buf);
        ret = -EINVAL;
    }
    
    kfree(cmd_buf);
    return ret;
}

/* Module initialization */
static int __init testsuspend_init(void)
{
    int ret;
    
    /* Allocate device number */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("testsuspend: Failed to allocate device number\n");
        return ret;
    }
    
    /* Create device class */
    testsuspend_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(testsuspend_class)) {
        unregister_chrdev_region(dev_num, 1);
        pr_err("testsuspend: Failed to create device class\n");
        return PTR_ERR(testsuspend_class);
    }
    
    /* Initialize and add the character device */
    cdev_init(&testsuspend_cdev, &testsuspend_fops);
    testsuspend_cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&testsuspend_cdev, dev_num, 1);
    if (ret < 0) {
        class_destroy(testsuspend_class);
        unregister_chrdev_region(dev_num, 1);
        pr_err("testsuspend: Failed to add character device\n");
        return ret;
    }
    
    /* Create device file */
    testsuspend_device = device_create(testsuspend_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(testsuspend_device)) {
        cdev_del(&testsuspend_cdev);
        class_destroy(testsuspend_class);
        unregister_chrdev_region(dev_num, 1);
        pr_err("testsuspend: Failed to create device\n");
        return PTR_ERR(testsuspend_device);
    }
    
    /* Create wakeup source */
    testsuspend_ws = wakeup_source_register(testsuspend_device, "testsuspend_ws");
    if (!testsuspend_ws) {
        pr_warn("testsuspend: Failed to register wakeup source\n");
    }
    
    /* Initialize completion and create suspend simulation thread */
    init_completion(&suspend_completion);
    suspend_thread = kthread_run(suspend_thread_fn, NULL, "testsuspend_thread");
    if (IS_ERR(suspend_thread)) {
        wakeup_source_unregister(testsuspend_ws);
        device_destroy(testsuspend_class, dev_num);
        cdev_del(&testsuspend_cdev);
        class_destroy(testsuspend_class);
        unregister_chrdev_region(dev_num, 1);
        pr_err("testsuspend: Failed to create suspend thread\n");
        return PTR_ERR(suspend_thread);
    }
    
    pr_info("testsuspend: Module loaded successfully\n");
    return 0;
}

/* Module cleanup */
static void __exit testsuspend_exit(void)
{
    if (suspend_thread) {
        kthread_stop(suspend_thread);
        complete(&suspend_completion); /* Wake it up to check stop condition */
    }
    
    if (testsuspend_ws)
        wakeup_source_unregister(testsuspend_ws);
    
    device_destroy(testsuspend_class, dev_num);
    cdev_del(&testsuspend_cdev);
    class_destroy(testsuspend_class);
    unregister_chrdev_region(dev_num, 1);
    
    pr_info("testsuspend: Module unloaded\n");
}

module_init(testsuspend_init);
module_exit(testsuspend_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("Device driver to simulate suspend/resume cycles");
MODULE_VERSION("1.0");
