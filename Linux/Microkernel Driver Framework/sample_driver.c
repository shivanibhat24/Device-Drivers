// sample_driver.c - Example driver that loads into our microkernel
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include "microkernel.h"

#define SAMPLE_DRIVER_NAME "sample_driver"
#define BUFFER_SIZE 256

struct sample_data {
    char buffer[BUFFER_SIZE];
    size_t buffer_size;
    struct mutex lock;
};

static struct micro_driver sample_driver;
static struct sample_data *driver_data;

static int sample_init(struct micro_driver *drv, void *data)
{
    pr_info("Sample driver initialized\n");
    
    driver_data = kzalloc(sizeof(*driver_data), GFP_KERNEL);
    if (!driver_data)
        return -ENOMEM;
    
    mutex_init(&driver_data->lock);
    snprintf(driver_data->buffer, BUFFER_SIZE, "Sample driver data\n");
    driver_data->buffer_size = strlen(driver_data->buffer);
    
    drv->private_data = driver_data;
    return 0;
}

static void sample_exit(struct micro_driver *drv)
{
    pr_info("Sample driver exiting\n");
    
    if (driver_data) {
        mutex_destroy(&driver_data->lock);
        kfree(driver_data);
        driver_data = NULL;
    }
    
    drv->private_data = NULL;
}

static int sample_open(struct micro_driver *drv, struct file *filp)
{
    pr_debug("Sample driver opened\n");
    return 0;
}

static int sample_release(struct micro_driver *drv, struct file *filp)
{
    pr_debug("Sample driver released\n");
    return 0;
}

static ssize_t sample_read(struct micro_driver *drv, struct file *filp, 
                          char __user *buf, size_t count, loff_t *offset)
{
    struct sample_data *data = drv->private_data;
    ssize_t ret;
    
    if (!data)
        return -EINVAL;
    
    mutex_lock(&data->lock);
    
    if (*offset >= data->buffer_size) {
        ret = 0; // EOF
        goto out;
    }
    
    if (*offset + count > data->buffer_size)
        count = data->buffer_size - *offset;
    
    if (copy_to_user(buf, data->buffer + *offset, count)) {
        ret = -EFAULT;
        goto out;
    }
    
    *offset += count;
    ret = count;
    
out:
    mutex_unlock(&data->lock);
    return ret;
}

static ssize_t sample_write(struct micro_driver *drv, struct file *filp,
                           const char __user *buf, size_t count, loff_t *offset)
{
    struct sample_data *data = drv->private_data;
    ssize_t ret;
    
    if (!data)
        return -EINVAL;
    
    mutex_lock(&data->lock);
    
    if (count > BUFFER_SIZE - 1)
        count = BUFFER_SIZE - 1;
    
    if (copy_from_user(data->buffer, buf, count)) {
        ret = -EFAULT;
        goto out;
    }
    
    data->buffer[count] = '\0';
    data->buffer_size = count;
    *offset = count;
    ret = count;
    
out:
    mutex_unlock(&data->lock);
    return ret;
}

static struct micro_device *sample_device;

static int __init sample_driver_init(void)
{
    int ret;
    
    // Initialize and register the driver
    sample_driver.name = SAMPLE_DRIVER_NAME;
    sample_driver.init = sample_init;
    sample_driver.exit = sample_exit;
    sample_driver.open = sample_open;
    sample_driver.release = sample_release;
    sample_driver.read = sample_read;
    sample_driver.write = sample_write;
    
    ret = micro_register_driver(&sample_driver);
    if (ret) {
        pr_err("Failed to register sample driver: %d\n", ret);
        return ret;
    }
    
    // Create a device for this driver
    sample_device = micro_create_device(SAMPLE_DRIVER_NAME, NULL);
    if (IS_ERR(sample_device)) {
        ret = PTR_ERR(sample_device);
        pr_err("Failed to create sample device: %d\n", ret);
        micro_unregister_driver(&sample_driver);
        return ret;
    }
    
    pr_info("Sample driver module loaded\n");
    return 0;
}

static void __exit sample_driver_exit(void)
{
    micro_destroy_device(sample_device);
    micro_unregister_driver(&sample_driver);
    pr_info("Sample driver module unloaded\n");
}

module_init(sample_driver_init);
module_exit(sample_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Sample Driver for Microkernel Framework");
MODULE_VERSION("0.1");
