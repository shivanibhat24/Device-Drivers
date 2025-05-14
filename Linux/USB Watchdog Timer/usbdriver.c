/*
 * usbwatchdog.c - USB Watchdog Driver
 *
 * This driver creates a /dev/usbwatchdog device that:
 * - Maintains a whitelist of allowed USB devices
 * - Logs all non-whitelisted USB devices when connected
 * - Provides configuration interface through ioctl
 *
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/list.h>

#define DRIVER_AUTHOR "USB Security Team"
#define DRIVER_DESC "USB Watchdog Driver for Device Control"
#define DEVICE_NAME "usbwatchdog"
#define MAX_WHITELIST_ENTRIES 256
#define MAX_MANUFACTURER_LEN 64
#define MAX_PRODUCT_LEN 64

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");

/* Device structure for our whitelist */
struct whitelist_device {
    u16 vendor_id;
    u16 product_id;
    u16 device_class;
    char manufacturer[MAX_MANUFACTURER_LEN];
    char product[MAX_PRODUCT_LEN];
    struct list_head list;
};

/* IOCTLs for the device */
#define USBWATCHDOG_IOC_MAGIC 'u'
#define USBWATCHDOG_ADD_DEVICE _IOW(USBWATCHDOG_IOC_MAGIC, 1, struct whitelist_device)
#define USBWATCHDOG_REMOVE_DEVICE _IOW(USBWATCHDOG_IOC_MAGIC, 2, struct whitelist_device)
#define USBWATCHDOG_CLEAR_WHITELIST _IO(USBWATCHDOG_IOC_MAGIC, 3)
#define USBWATCHDOG_GET_WHITELIST _IOR(USBWATCHDOG_IOC_MAGIC, 4, struct whitelist_device*)
#define USBWATCHDOG_SET_LOG_LEVEL _IOW(USBWATCHDOG_IOC_MAGIC, 5, int)

/* Global variables */
static int usbwatchdog_major;
static struct class *usbwatchdog_class;
static struct device *usbwatchdog_device;
static struct cdev usbwatchdog_cdev;
static LIST_HEAD(whitelist_head);
static DEFINE_MUTEX(whitelist_mutex);
static int log_level = 1; /* 0 = quiet, 1 = normal, 2 = verbose */
static int whitelist_count = 0;

/* Function to check if a device is in the whitelist */
static bool is_device_whitelisted(struct usb_device *dev)
{
    struct whitelist_device *entry;
    bool found = false;

    mutex_lock(&whitelist_mutex);
    
    list_for_each_entry(entry, &whitelist_head, list) {
        /* Check by vendor/product ID first */
        if (entry->vendor_id == le16_to_cpu(dev->descriptor.idVendor) &&
            entry->product_id == le16_to_cpu(dev->descriptor.idProduct)) {
            found = true;
            break;
        }
        
        /* Check by device class if entry specifies it */
        if (entry->device_class != 0 && 
            entry->device_class == dev->descriptor.bDeviceClass) {
            found = true;
            break;
        }
    }
    
    mutex_unlock(&whitelist_mutex);
    return found;
}

/* USB device connection notifier */
static int usb_notify(struct notifier_block *self, unsigned long action, void *dev)
{
    struct usb_device *udev = (struct usb_device *)dev;
    char manufacturer[MAX_MANUFACTURER_LEN] = "Unknown";
    char product[MAX_PRODUCT_LEN] = "Unknown";
    
    if (action != USB_DEVICE_ADD)
        return NOTIFY_OK;

    /* Try to get device strings */
    if (udev->manufacturer)
        strncpy(manufacturer, udev->manufacturer, MAX_MANUFACTURER_LEN - 1);
    if (udev->product)
        strncpy(product, udev->product, MAX_PRODUCT_LEN - 1);

    if (!is_device_whitelisted(udev)) {
        /* Log unauthorized device */
        printk(KERN_WARNING "USBWATCHDOG: Unauthorized USB device detected!\n");
        printk(KERN_WARNING "USBWATCHDOG: VendorID: %04x, ProductID: %04x\n", 
            le16_to_cpu(udev->descriptor.idVendor), le16_to_cpu(udev->descriptor.idProduct));
        
        if (log_level >= 1) {
            printk(KERN_WARNING "USBWATCHDOG: Manufacturer: %s\n", manufacturer);
            printk(KERN_WARNING "USBWATCHDOG: Product: %s\n", product);
        }
        
        if (log_level >= 2) {
            printk(KERN_WARNING "USBWATCHDOG: Device Class: %d\n", udev->descriptor.bDeviceClass);
            printk(KERN_WARNING "USBWATCHDOG: Device Address: %d\n", udev->devnum);
            printk(KERN_WARNING "USBWATCHDOG: Bus Number: %d\n", udev->bus->busnum);
        }
    } else if (log_level >= 2) {
        /* Log authorized device in verbose mode */
        printk(KERN_INFO "USBWATCHDOG: Authorized USB device connected\n");
        printk(KERN_INFO "USBWATCHDOG: VendorID: %04x, ProductID: %04x\n", 
            le16_to_cpu(udev->descriptor.idVendor), le16_to_cpu(udev->descriptor.idProduct));
        printk(KERN_INFO "USBWATCHDOG: Manufacturer: %s\n", manufacturer);
        printk(KERN_INFO "USBWATCHDOG: Product: %s\n", product);
    }

    return NOTIFY_OK;
}

static struct notifier_block usb_notify_block = {
    .notifier_call = usb_notify,
};

/* Device file operations */
static int device_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t device_read(struct file *file, char __user *buffer, size_t length, loff_t *offset)
{
    char *status_buffer;
    size_t len;
    ssize_t ret = 0;
    struct whitelist_device *entry;
    int count = 0;

    /* Allocate a temporary buffer */
    status_buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!status_buffer)
        return -ENOMEM;

    len = scnprintf(status_buffer, PAGE_SIZE, "USB Watchdog Status\n");
    len += scnprintf(status_buffer + len, PAGE_SIZE - len, "-------------------\n");
    len += scnprintf(status_buffer + len, PAGE_SIZE - len, "Log Level: %d\n", log_level);
    len += scnprintf(status_buffer + len, PAGE_SIZE - len, "Whitelist Entries: %d\n\n", whitelist_count);

    mutex_lock(&whitelist_mutex);
    list_for_each_entry(entry, &whitelist_head, list) {
        if (len > PAGE_SIZE - 100) {
            len += scnprintf(status_buffer + len, PAGE_SIZE - len, "... (more entries)\n");
            break;
        }
        
        len += scnprintf(status_buffer + len, PAGE_SIZE - len, 
                      "[%d] VID:PID = %04x:%04x Class: %02x\n", 
                      ++count, entry->vendor_id, entry->product_id, entry->device_class);
        
        if (strlen(entry->manufacturer) > 0)
            len += scnprintf(status_buffer + len, PAGE_SIZE - len, 
                          "    Manufacturer: %s\n", entry->manufacturer);
        
        if (strlen(entry->product) > 0)
            len += scnprintf(status_buffer + len, PAGE_SIZE - len, 
                          "    Product: %s\n", entry->product);
    }
    mutex_unlock(&whitelist_mutex);

    if (*offset >= len) {
        kfree(status_buffer);
        return 0;
    }

    if (*offset + length > len)
        length = len - *offset;

    if (copy_to_user(buffer, status_buffer + *offset, length)) {
        ret = -EFAULT;
    } else {
        *offset += length;
        ret = length;
    }

    kfree(status_buffer);
    return ret;
}

static ssize_t device_write(struct file *file, const char __user *buffer, size_t length, loff_t *offset)
{
    /* We don't support direct writing to the device */
    return -EINVAL;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct whitelist_device *new_entry, *entry, *tmp;
    struct whitelist_device user_device;
    int new_log_level;

    switch (cmd) {
        case USBWATCHDOG_ADD_DEVICE:
            if (whitelist_count >= MAX_WHITELIST_ENTRIES)
                return -ENOSPC;
                
            if (copy_from_user(&user_device, (struct whitelist_device __user *)arg, sizeof(struct whitelist_device)))
                return -EFAULT;
                
            new_entry = kmalloc(sizeof(struct whitelist_device), GFP_KERNEL);
            if (!new_entry)
                return -ENOMEM;
                
            memcpy(new_entry, &user_device, sizeof(struct whitelist_device));
            INIT_LIST_HEAD(&new_entry->list);
            
            mutex_lock(&whitelist_mutex);
            list_add(&new_entry->list, &whitelist_head);
            whitelist_count++;
            mutex_unlock(&whitelist_mutex);
            
            if (log_level >= 1) {
                printk(KERN_INFO "USBWATCHDOG: Added device to whitelist: %04x:%04x\n",
                       new_entry->vendor_id, new_entry->product_id);
            }
            return 0;
            
        case USBWATCHDOG_REMOVE_DEVICE:
            if (copy_from_user(&user_device, (struct whitelist_device __user *)arg, sizeof(struct whitelist_device)))
                return -EFAULT;
                
            mutex_lock(&whitelist_mutex);
            list_for_each_entry_safe(entry, tmp, &whitelist_head, list) {
                if (entry->vendor_id == user_device.vendor_id && 
                    entry->product_id == user_device.product_id) {
                    list_del(&entry->list);
                    kfree(entry);
                    whitelist_count--;
                    
                    if (log_level >= 1) {
                        printk(KERN_INFO "USBWATCHDOG: Removed device from whitelist: %04x:%04x\n",
                              user_device.vendor_id, user_device.product_id);
                    }
                    break;
                }
            }
            mutex_unlock(&whitelist_mutex);
            return 0;
            
        case USBWATCHDOG_CLEAR_WHITELIST:
            mutex_lock(&whitelist_mutex);
            list_for_each_entry_safe(entry, tmp, &whitelist_head, list) {
                list_del(&entry->list);
                kfree(entry);
            }
            whitelist_count = 0;
            mutex_unlock(&whitelist_mutex);
            
            if (log_level >= 1) {
                printk(KERN_INFO "USBWATCHDOG: Whitelist cleared\n");
            }
            return 0;
            
        case USBWATCHDOG_SET_LOG_LEVEL:
            if (copy_from_user(&new_log_level, (int __user *)arg, sizeof(int)))
                return -EFAULT;
                
            if (new_log_level < 0 || new_log_level > 2)
                return -EINVAL;
                
            log_level = new_log_level;
            printk(KERN_INFO "USBWATCHDOG: Log level set to %d\n", log_level);
            return 0;
            
        default:
            return -ENOTTY;
    }
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_release,
    .read = device_read,
    .write = device_write,
    .unlocked_ioctl = device_ioctl,
};

static int __init usbwatchdog_init(void)
{
    int ret;
    dev_t dev;

    /* Register the character device */
    ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "USBWATCHDOG: Failed to allocate device number\n");
        return ret;
    }
    
    usbwatchdog_major = MAJOR(dev);
    
    /* Create device class */
    usbwatchdog_class = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(usbwatchdog_class)) {
        ret = PTR_ERR(usbwatchdog_class);
        printk(KERN_ERR "USBWATCHDOG: Failed to create device class\n");
        goto error_class;
    }
    
    /* Create device file */
    usbwatchdog_device = device_create(usbwatchdog_class, NULL, 
                                 MKDEV(usbwatchdog_major, 0),
                                 NULL, DEVICE_NAME);
    if (IS_ERR(usbwatchdog_device)) {
        ret = PTR_ERR(usbwatchdog_device);
        printk(KERN_ERR "USBWATCHDOG: Failed to create device\n");
        goto error_device;
    }
    
    /* Initialize character device */
    cdev_init(&usbwatchdog_cdev, &fops);
    usbwatchdog_cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&usbwatchdog_cdev, MKDEV(usbwatchdog_major, 0), 1);
    if (ret < 0) {
        printk(KERN_ERR "USBWATCHDOG: Failed to add character device\n");
        goto error_cdev;
    }
    
    /* Register USB notifier */
    ret = usb_register_notify(&usb_notify_block);
    if (ret < 0) {
        printk(KERN_ERR "USBWATCHDOG: Failed to register USB notifier\n");
        goto error_notifier;
    }
    
    printk(KERN_INFO "USBWATCHDOG: USB Watchdog Driver loaded successfully\n");
    return 0;

error_notifier:
    cdev_del(&usbwatchdog_cdev);
error_cdev:
    device_destroy(usbwatchdog_class, MKDEV(usbwatchdog_major, 0));
error_device:
    class_destroy(usbwatchdog_class);
error_class:
    unregister_chrdev_region(MKDEV(usbwatchdog_major, 0), 1);
    return ret;
}

static void __exit usbwatchdog_exit(void)
{
    struct whitelist_device *entry, *tmp;
    
    /* Clean up USB notifier */
    usb_unregister_notify(&usb_notify_block);
    
    /* Remove character device */
    cdev_del(&usbwatchdog_cdev);
    device_destroy(usbwatchdog_class, MKDEV(usbwatchdog_major, 0));
    class_destroy(usbwatchdog_class);
    unregister_chrdev_region(MKDEV(usbwatchdog_major, 0), 1);
    
    /* Free whitelist entries */
    list_for_each_entry_safe(entry, tmp, &whitelist_head, list) {
        list_del(&entry->list);
        kfree(entry);
    }
    
    printk(KERN_INFO "USBWATCHDOG: USB Watchdog Driver unloaded\n");
}

module_init(usbwatchdog_init);
module_exit(usbwatchdog_exit);
