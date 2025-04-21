// microkernel.c - Core implementation of our microkernel-like module
#include "microkernel.h"

static int micro_major;
static struct class *micro_class;
struct bus_type micro_bus;

static LIST_HEAD(devices_list);
static DEFINE_MUTEX(devices_mutex);

// Character device file operations
static int micro_open(struct inode *inode, struct file *filp)
{
    struct micro_device *mdev = container_of(inode->i_cdev, struct micro_device, cdev);
    filp->private_data = mdev;
    
    if (mdev->driver && mdev->driver->open)
        return mdev->driver->open(mdev->driver, filp);
    
    return 0;
}

static int micro_release(struct inode *inode, struct file *filp)
{
    struct micro_device *mdev = filp->private_data;
    
    if (mdev->driver && mdev->driver->release)
        return mdev->driver->release(mdev->driver, filp);
    
    return 0;
}

static ssize_t micro_read(struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
    struct micro_device *mdev = filp->private_data;
    
    if (mdev->driver && mdev->driver->read)
        return mdev->driver->read(mdev->driver, filp, buf, count, offset);
    
    return -EINVAL;
}

static ssize_t micro_write(struct file *filp, const char __user *buf, size_t count, loff_t *offset)
{
    struct micro_device *mdev = filp->private_data;
    
    if (mdev->driver && mdev->driver->write)
        return mdev->driver->write(mdev->driver, filp, buf, count, offset);
    
    return -EINVAL;
}

static long micro_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct micro_device *mdev = filp->private_data;
    
    if (mdev->driver && mdev->driver->ioctl)
        return mdev->driver->ioctl(mdev->driver, filp, cmd, arg);
    
    return -ENOTTY;
}

static const struct file_operations micro_fops = {
    .owner = THIS_MODULE,
    .open = micro_open,
    .release = micro_release,
    .read = micro_read,
    .write = micro_write,
    .unlocked_ioctl = micro_ioctl,
};

// Bus match function - determines if driver can be bound to a device
static int micro_match(struct device *dev, struct device_driver *drv)
{
    struct micro_device *mdev = micro_get_device(dev);
    struct micro_driver *mdrv = micro_get_driver(drv);
    
    if (!mdev || !mdrv)
        return 0;
    
    // Simple name matching
    return strcmp(mdev->name, mdrv->name) == 0;
}

// Bus probe function - called when device is attached to a driver
static int micro_probe(struct device *dev)
{
    struct micro_device *mdev = micro_get_device(dev);
    struct micro_driver *mdrv = micro_get_driver(dev->driver);
    
    if (!mdev || !mdrv)
        return -EINVAL;
    
    mdev->driver = mdrv;
    
    if (mdrv->init)
        return mdrv->init(mdrv, mdev->private_data);
    
    return 0;
}

// Bus remove function - called when device is detached from driver
static int micro_remove(struct device *dev)
{
    struct micro_device *mdev = micro_get_device(dev);
    struct micro_driver *mdrv;
    
    if (!mdev || !mdev->driver)
        return -EINVAL;
    
    mdrv = mdev->driver;
    
    if (mdrv->exit)
        mdrv->exit(mdrv);
    
    mdev->driver = NULL;
    
    return 0;
}

struct bus_type micro_bus = {
    .name = MICRO_BUS_NAME,
    .match = micro_match,
    .probe = micro_probe,
    .remove = micro_remove,
};

// Helper functions
struct micro_driver *micro_get_driver(struct device_driver *driver)
{
    if (!driver)
        return NULL;
    
    return container_of(driver, struct micro_driver, driver);
}

struct micro_device *micro_get_device(struct device *dev)
{
    if (!dev)
        return NULL;
    
    return container_of(dev, struct micro_device, dev);
}

// Driver registration
int micro_register_driver(struct micro_driver *driver)
{
    if (!driver || !driver->name)
        return -EINVAL;
    
    driver->driver.name = driver->name;
    driver->driver.bus = &micro_bus;
    driver->driver.owner = THIS_MODULE;
    
    return driver_register(&driver->driver);
}
EXPORT_SYMBOL_GPL(micro_register_driver);

void micro_unregister_driver(struct micro_driver *driver)
{
    if (!driver)
        return;
    
    driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(micro_unregister_driver);

// Device creation
struct micro_device *micro_create_device(const char *name, void *data)
{
    struct micro_device *mdev;
    dev_t dev;
    int err;
    
    if (!name)
        return ERR_PTR(-EINVAL);
    
    mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
    if (!mdev)
        return ERR_PTR(-ENOMEM);
    
    mdev->name = kstrdup(name, GFP_KERNEL);
    if (!mdev->name) {
        kfree(mdev);
        return ERR_PTR(-ENOMEM);
    }
    
    mutex_lock(&devices_mutex);
    
    // Allocate device number
    err = alloc_chrdev_region(&dev, 0, 1, name);
    if (err) {
        mutex_unlock(&devices_mutex);
        kfree(mdev->name);
        kfree(mdev);
        return ERR_PTR(err);
    }
    
    mdev->devt = dev;
    mdev->private_data = data;
    
    // Initialize cdev
    cdev_init(&mdev->cdev, &micro_fops);
    mdev->cdev.owner = THIS_MODULE;
    err = cdev_add(&mdev->cdev, dev, 1);
    if (err) {
        unregister_chrdev_region(dev, 1);
        mutex_unlock(&devices_mutex);
        kfree(mdev->name);
        kfree(mdev);
        return ERR_PTR(err);
    }
    
    // Initialize and register device
    device_initialize(&mdev->dev);
    mdev->dev.bus = &micro_bus;
    mdev->dev.type = NULL;
    mdev->dev.parent = NULL;
    mdev->dev.devt = dev;
    dev_set_name(&mdev->dev, "%s", name);
    
    // Create device node in /dev
    device_create(micro_class, NULL, dev, NULL, "%s", name);
    
    err = device_add(&mdev->dev);
    if (err) {
        device_destroy(micro_class, dev);
        cdev_del(&mdev->cdev);
        unregister_chrdev_region(dev, 1);
        mutex_unlock(&devices_mutex);
        kfree(mdev->name);
        kfree(mdev);
        return ERR_PTR(err);
    }
    
    mutex_unlock(&devices_mutex);
    
    return mdev;
}
EXPORT_SYMBOL_GPL(micro_create_device);

void micro_destroy_device(struct micro_device *mdev)
{
    if (!mdev)
        return;
    
    mutex_lock(&devices_mutex);
    
    device_destroy(micro_class, mdev->devt);
    device_unregister(&mdev->dev);
    cdev_del(&mdev->cdev);
    unregister_chrdev_region(mdev->devt, 1);
    
    mutex_unlock(&devices_mutex);
    
    kfree(mdev->name);
    kfree(mdev);
}
EXPORT_SYMBOL_GPL(micro_destroy_device);

static int __init microkernel_init(void)
{
    int ret;
    
    // Register bus type
    ret = bus_register(&micro_bus);
    if (ret) {
        pr_err("Failed to register micro bus: %d\n", ret);
        return ret;
    }
    
    // Create class
    micro_class = class_create(THIS_MODULE, MICRO_CLASS_NAME);
    if (IS_ERR(micro_class)) {
        ret = PTR_ERR(micro_class);
        pr_err("Failed to create micro class: %d\n", ret);
        bus_unregister(&micro_bus);
        return ret;
    }
    
    pr_info("Microkernel module loaded\n");
    return 0;
}

static void __exit microkernel_exit(void)
{
    class_destroy(micro_class);
    bus_unregister(&micro_bus);
    pr_info("Microkernel module unloaded\n");
}

module_init(microkernel_init);
module_exit(microkernel_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Microkernel-like Loadable Driver Framework");
MODULE_VERSION("0.1");
