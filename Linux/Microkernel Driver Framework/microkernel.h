// microkernel.h - Header file for our microkernel-like module
#ifndef _MICROKERNEL_H
#define _MICROKERNEL_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#define MICRO_DEVICE_NAME "microkernel"
#define MICRO_CLASS_NAME "microkernel"
#define MICRO_BUS_NAME "micro_bus"

struct micro_driver {
    const char *name;
    int (*init)(struct micro_driver *drv, void *data);
    void (*exit)(struct micro_driver *drv);
    int (*open)(struct micro_driver *drv, struct file *filp);
    int (*release)(struct micro_driver *drv, struct file *filp);
    ssize_t (*read)(struct micro_driver *drv, struct file *filp, char __user *buf, size_t count, loff_t *offset);
    ssize_t (*write)(struct micro_driver *drv, struct file *filp, const char __user *buf, size_t count, loff_t *offset);
    long (*ioctl)(struct micro_driver *drv, struct file *filp, unsigned int cmd, unsigned long arg);
    
    struct device_driver driver;  // Embedded driver structure
    void *private_data;           // Driver's private data
};

struct micro_device {
    const char *name;
    struct micro_driver *driver;
    struct device dev;         // Embedded device structure
    struct cdev cdev;          // Character device
    dev_t devt;                // Device number
    void *private_data;        // Device-specific private data
};

// Function declarations for micro_bus
extern struct bus_type micro_bus;
extern struct class *micro_class;

// Registration functions
extern int micro_register_driver(struct micro_driver *driver);
extern void micro_unregister_driver(struct micro_driver *driver);
extern struct micro_device *micro_create_device(const char *name, void *data);
extern void micro_destroy_device(struct micro_device *device);

// Helper functions
extern struct micro_driver *micro_get_driver(struct device_driver *driver);
extern struct micro_device *micro_get_device(struct device *dev);

#endif /* _MICROKERNEL_H */
