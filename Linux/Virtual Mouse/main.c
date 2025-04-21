/*
 * virtual_mouse.c - A virtual mouse driver for Linux
 *
 * This module creates a virtual input device that simulates mouse movements
 * and can be controlled from kernel space.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "virtual_mouse"
#define PROC_ENTRY_NAME "vmouse"
#define PROC_ENTRY_PERMS 0666

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("Virtual Mouse Driver");
MODULE_VERSION("1.0");

struct virtual_mouse {
    struct input_dev *input_dev;
    int x;
    int y;
    struct proc_dir_entry *proc_entry;
};

static struct virtual_mouse *vmouse;

/* Proc file operations for user space control */
static ssize_t vmouse_proc_read(struct file *file, char __user *buffer, size_t count, loff_t *offset)
{
    char buf[64];
    int len;
    
    if (*offset > 0)
        return 0;
    
    len = snprintf(buf, sizeof(buf), "Current position: x=%d, y=%d\n", vmouse->x, vmouse->y);
    
    if (copy_to_user(buffer, buf, len))
        return -EFAULT;
    
    *offset += len;
    return len;
}

static ssize_t vmouse_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *offset)
{
    char buf[64];
    int x, y, ret;
    
    if (count > sizeof(buf) - 1)
        return -EINVAL;
    
    if (copy_from_user(buf, buffer, count))
        return -EFAULT;
    
    buf[count] = '\0';
    
    /* Parse input format: "x,y" */
    ret = sscanf(buf, "%d,%d", &x, &y);
    if (ret != 2)
        return -EINVAL;
    
    /* Update mouse position */
    vmouse->x = x;
    vmouse->y = y;
    
    /* Report absolute position */
    input_report_abs(vmouse->input_dev, ABS_X, x);
    input_report_abs(vmouse->input_dev, ABS_Y, y);
    input_sync(vmouse->input_dev);
    
    printk(KERN_INFO DEVICE_NAME ": moved cursor to (%d, %d)\n", x, y);
    
    return count;
}

/* Function to move the cursor from kernel code */
void vmouse_move_cursor(int x, int y)
{
    if (!vmouse || !vmouse->input_dev)
        return;
    
    vmouse->x = x;
    vmouse->y = y;
    
    input_report_abs(vmouse->input_dev, ABS_X, x);
    input_report_abs(vmouse->input_dev, ABS_Y, y);
    input_sync(vmouse->input_dev);
    
    printk(KERN_INFO DEVICE_NAME ": kernel moved cursor to (%d, %d)\n", x, y);
}
EXPORT_SYMBOL_GPL(vmouse_move_cursor);

/* Function to click the mouse button from kernel code */
void vmouse_click(int button)
{
    if (!vmouse || !vmouse->input_dev)
        return;
    
    /* Press button */
    input_report_key(vmouse->input_dev, button, 1);
    input_sync(vmouse->input_dev);
    
    /* Release button (small delay happens naturally) */
    input_report_key(vmouse->input_dev, button, 0);
    input_sync(vmouse->input_dev);
    
    printk(KERN_INFO DEVICE_NAME ": clicked button %d\n", button);
}
EXPORT_SYMBOL_GPL(vmouse_click);

static const struct proc_ops vmouse_proc_ops = {
    .proc_read = vmouse_proc_read,
    .proc_write = vmouse_proc_write,
};

static int __init virtual_mouse_init(void)
{
    int ret;
    
    /* Allocate memory for our device state */
    vmouse = kzalloc(sizeof(struct virtual_mouse), GFP_KERNEL);
    if (!vmouse) {
        printk(KERN_ERR DEVICE_NAME ": failed to allocate memory\n");
        return -ENOMEM;
    }
    
    /* Allocate and configure input device */
    vmouse->input_dev = input_allocate_device();
    if (!vmouse->input_dev) {
        printk(KERN_ERR DEVICE_NAME ": failed to allocate input device\n");
        ret = -ENOMEM;
        goto err_free_vmouse;
    }
    
    vmouse->input_dev->name = DEVICE_NAME;
    vmouse->input_dev->phys = "virtual/mouse";
    vmouse->input_dev->id.bustype = BUS_VIRTUAL;
    vmouse->input_dev->id.vendor = 0x0000;
    vmouse->input_dev->id.product = 0x0000;
    vmouse->input_dev->id.version = 0x0100;
    
    /* Set capabilities */
    __set_bit(EV_KEY, vmouse->input_dev->evbit);
    __set_bit(EV_ABS, vmouse->input_dev->evbit);
    __set_bit(BTN_LEFT, vmouse->input_dev->keybit);
    __set_bit(BTN_RIGHT, vmouse->input_dev->keybit);
    __set_bit(BTN_MIDDLE, vmouse->input_dev->keybit);
    
    /* Set absolute coordinate ranges - adjust for your screen resolution */
    input_set_abs_params(vmouse->input_dev, ABS_X, 0, 1920, 0, 0);
    input_set_abs_params(vmouse->input_dev, ABS_Y, 0, 1080, 0, 0);
    
    /* Register the input device */
    ret = input_register_device(vmouse->input_dev);
    if (ret) {
        printk(KERN_ERR DEVICE_NAME ": failed to register input device\n");
        goto err_free_input;
    }
    
    /* Create proc entry for user control */
    vmouse->proc_entry = proc_create(PROC_ENTRY_NAME, PROC_ENTRY_PERMS, NULL, &vmouse_proc_ops);
    if (!vmouse->proc_entry) {
        printk(KERN_ERR DEVICE_NAME ": failed to create proc entry\n");
        ret = -ENOMEM;
        goto err_unregister_input;
    }
    
    printk(KERN_INFO DEVICE_NAME ": virtual mouse initialized\n");
    return 0;
    
err_unregister_input:
    input_unregister_device(vmouse->input_dev);
    /* No need to free input_dev here as input_unregister_device() does it */
    return ret;
    
err_free_input:
    input_free_device(vmouse->input_dev);
    
err_free_vmouse:
    kfree(vmouse);
    return ret;
}

static void __exit virtual_mouse_exit(void)
{
    /* Remove the proc entry */
    proc_remove(vmouse->proc_entry);
    
    /* Unregister the input device */
    input_unregister_device(vmouse->input_dev);
    
    /* Free the device structure */
    kfree(vmouse);
    
    printk(KERN_INFO DEVICE_NAME ": virtual mouse removed\n");
}

module_init(virtual_mouse_init);
module_exit(virtual_mouse_exit);
