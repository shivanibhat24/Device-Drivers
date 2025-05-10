/*
 * Bit-Banged UART Driver
 * Using hrtimer and GPIO to emulate UART functionality
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>

#define DEVICE_NAME "bbuart"
#define CLASS_NAME "bbuart"
#define FIFO_SIZE 1024
#define DEFAULT_BAUD_RATE 9600
#define MAX_DEVICES 4

/* Bit-banged UART structure */
struct bbuart_dev {
    struct cdev cdev;
    int minor;
    
    /* GPIO pins */
    unsigned int tx_pin;
    unsigned int rx_pin;
    
    /* Timing parameters */
    unsigned int baud_rate;
    ktime_t bit_time;
    
    /* TX state machine */
    struct hrtimer tx_timer;
    unsigned char tx_byte;
    int tx_bit_pos;
    int tx_busy;
    
    /* RX state machine */
    struct hrtimer rx_timer;
    unsigned char rx_byte;
    int rx_bit_pos;
    int rx_busy;
    
    /* Data buffers */
    DECLARE_KFIFO(tx_fifo, unsigned char, FIFO_SIZE);
    DECLARE_KFIFO(rx_fifo, unsigned char, FIFO_SIZE);
    
    /* Synchronization */
    struct mutex tx_mutex;
    struct mutex rx_mutex;
};

static int bbuart_major;
static struct class *bbuart_class;
static struct bbuart_dev *bbuart_devices[MAX_DEVICES];

static struct gpio_desc *tx_gpio_desc;
static struct gpio_desc *rx_gpio_desc;

/* Transmit one bit using GPIO */
static void bbuart_tx_bit(struct bbuart_dev *dev, int bit)
{
    /* Set TX pin to the bit value (0 or 1) */
    gpio_set_value(dev->tx_pin, bit);
}

/* Receive one bit from GPIO */
static int bbuart_rx_bit(struct bbuart_dev *dev)
{
    /* Read the RX pin value */
    return gpio_get_value(dev->rx_pin);
}

/* HR Timer handler for transmitting bits */
static enum hrtimer_restart bbuart_tx_timer_handler(struct hrtimer *timer)
{
    struct bbuart_dev *dev = container_of(timer, struct bbuart_dev, tx_timer);
    
    if (dev->tx_bit_pos == 0) {
        /* Start bit (0) */
        bbuart_tx_bit(dev, 0);
        dev->tx_bit_pos++;
    } else if (dev->tx_bit_pos >= 1 && dev->tx_bit_pos <= 8) {
        /* Data bits (LSB first) */
        bbuart_tx_bit(dev, (dev->tx_byte >> (dev->tx_bit_pos - 1)) & 0x01);
        dev->tx_bit_pos++;
    } else if (dev->tx_bit_pos == 9) {
        /* Stop bit (1) */
        bbuart_tx_bit(dev, 1);
        dev->tx_bit_pos++;
    } else {
        /* TX complete, check if there's more data to send */
        unsigned char next_byte;
        if (!kfifo_is_empty(&dev->tx_fifo)) {
            mutex_lock(&dev->tx_mutex);
            kfifo_get(&dev->tx_fifo, &next_byte);
            mutex_unlock(&dev->tx_mutex);
            
            /* Start transmitting next byte */
            dev->tx_byte = next_byte;
            dev->tx_bit_pos = 0;
            
            /* Schedule for start bit */
            hrtimer_forward_now(timer, dev->bit_time);
            return HRTIMER_RESTART;
        } else {
            /* TX complete and no more data */
            dev->tx_busy = 0;
            return HRTIMER_NORESTART;
        }
    }
    
    /* Schedule next bit transmission */
    hrtimer_forward_now(timer, dev->bit_time);
    return HRTIMER_RESTART;
}

/* Start transmitting data from the TX FIFO */
static void bbuart_start_tx(struct bbuart_dev *dev)
{
    if (dev->tx_busy)
        return;
    
    /* Check if there's data to transmit */
    if (kfifo_is_empty(&dev->tx_fifo))
        return;
    
    /* Get first byte from FIFO */
    mutex_lock(&dev->tx_mutex);
    kfifo_get(&dev->tx_fifo, &dev->tx_byte);
    mutex_unlock(&dev->tx_mutex);
    
    /* Initialize TX state */
    dev->tx_bit_pos = 0;
    dev->tx_busy = 1;
    
    /* Start the timer for bit transmission */
    hrtimer_start(&dev->tx_timer, dev->bit_time, HRTIMER_MODE_REL);
}

/* HR Timer handler for receiving bits */
static enum hrtimer_restart bbuart_rx_timer_handler(struct hrtimer *timer)
{
    struct bbuart_dev *dev = container_of(timer, struct bbuart_dev, rx_timer);
    int bit_value;
    
    if (dev->rx_bit_pos == 0) {
        /* Check for start bit (0) */
        bit_value = bbuart_rx_bit(dev);
        if (bit_value == 0) {
            /* Valid start bit detected */
            dev->rx_bit_pos++;
            /* Schedule for middle of first data bit */
            hrtimer_forward_now(timer, ktime_add(dev->bit_time, ktime_divns(dev->bit_time, 2)));
            return HRTIMER_RESTART;
        } else {
            /* No start bit, check again after a while */
            hrtimer_forward_now(timer, ktime_divns(dev->bit_time, 4));
            return HRTIMER_RESTART;
        }
    } else if (dev->rx_bit_pos >= 1 && dev->rx_bit_pos <= 8) {
        /* Data bits (LSB first) */
        bit_value = bbuart_rx_bit(dev);
        dev->rx_byte |= (bit_value << (dev->rx_bit_pos - 1));
        dev->rx_bit_pos++;
    } else if (dev->rx_bit_pos == 9) {
        /* Stop bit (1) */
        bit_value = bbuart_rx_bit(dev);
        if (bit_value == 1) {
            /* Valid stop bit, add byte to RX FIFO */
            mutex_lock(&dev->rx_mutex);
            kfifo_put(&dev->rx_fifo, dev->rx_byte);
            mutex_unlock(&dev->rx_mutex);
        }
        
        /* Reset RX state */
        dev->rx_byte = 0;
        dev->rx_bit_pos = 0;
    }
    
    /* Schedule next bit reception */
    if (dev->rx_bit_pos > 0 && dev->rx_bit_pos <= 9) {
        hrtimer_forward_now(timer, dev->bit_time);
        return HRTIMER_RESTART;
    } else {
        /* Start looking for next byte immediately */
        hrtimer_forward_now(timer, ktime_set(0, 0));
        return HRTIMER_RESTART;
    }
}

/* File operations */
static int bbuart_open(struct inode *inode, struct file *filp)
{
    struct bbuart_dev *dev;
    dev = container_of(inode->i_cdev, struct bbuart_dev, cdev);
    filp->private_data = dev;
    return 0;
}

static int bbuart_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t bbuart_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct bbuart_dev *dev = filp->private_data;
    unsigned int copied;
    int ret;
    
    /* Read data from RX FIFO to user buffer */
    mutex_lock(&dev->rx_mutex);
    ret = kfifo_to_user(&dev->rx_fifo, buf, count, &copied);
    mutex_unlock(&dev->rx_mutex);
    
    if (ret)
        return -EFAULT;
        
    return copied;
}

static ssize_t bbuart_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct bbuart_dev *dev = filp->private_data;
    unsigned int copied;
    int ret;
    
    /* Write user data to TX FIFO */
    mutex_lock(&dev->tx_mutex);
    ret = kfifo_from_user(&dev->tx_fifo, buf, count, &copied);
    mutex_unlock(&dev->tx_mutex);
    
    if (ret)
        return -EFAULT;
    
    /* Start transmission if not already in progress */
    if (!dev->tx_busy)
        bbuart_start_tx(dev);
    
    return copied;
}

/* IOCTL handler for UART configuration */
static long bbuart_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct bbuart_dev *dev = filp->private_data;
    
    switch (cmd) {
    case 0x5401: /* Set baud rate */
        if (arg < 300 || arg > 115200)
            return -EINVAL;
        
        dev->baud_rate = arg;
        dev->bit_time = ktime_set(0, 1000000000 / dev->baud_rate);
        break;
        
    default:
        return -ENOTTY;
    }
    
    return 0;
}

static const struct file_operations bbuart_fops = {
    .owner = THIS_MODULE,
    .open = bbuart_open,
    .release = bbuart_release,
    .read = bbuart_read,
    .write = bbuart_write,
    .unlocked_ioctl = bbuart_ioctl,
};

/* Initialize a single UART device */
static int bbuart_init_device(struct bbuart_dev *dev, int minor, 
                              unsigned int tx_pin, unsigned int rx_pin)
{
    int ret;
    
    dev->minor = minor;
    dev->tx_pin = tx_pin;
    dev->rx_pin = rx_pin;
    
    /* Initialize GPIO pins */
    if (gpio_request(tx_pin, "bbuart_tx") < 0) {
        pr_err("Failed to request TX GPIO %d\n", tx_pin);
        return -ENODEV;
    }
    
    if (gpio_request(rx_pin, "bbuart_rx") < 0) {
        pr_err("Failed to request RX GPIO %d\n", rx_pin);
        gpio_free(tx_pin);
        return -ENODEV;
    }
    
    /* Configure GPIO directions */
    gpio_direction_output(tx_pin, 1); /* TX starts at idle (high) */
    gpio_direction_input(rx_pin);     /* RX as input */
    
    /* Initialize timing parameters */
    dev->baud_rate = DEFAULT_BAUD_RATE;
    dev->bit_time = ktime_set(0, 1000000000 / dev->baud_rate);
    
    /* Initialize TX/RX state */
    dev->tx_bit_pos = 0;
    dev->tx_busy = 0;
    dev->rx_bit_pos = 0;
    dev->rx_byte = 0;
    
    /* Initialize HR timers */
    hrtimer_init(&dev->tx_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    dev->tx_timer.function = bbuart_tx_timer_handler;
    
    hrtimer_init(&dev->rx_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    dev->rx_timer.function = bbuart_rx_timer_handler;
    
    /* Initialize FIFOs */
    ret = kfifo_init(&dev->tx_fifo, NULL, FIFO_SIZE);
    if (ret) {
        pr_err("Failed to initialize TX FIFO\n");
        goto err_gpio;
    }
    
    ret = kfifo_init(&dev->rx_fifo, NULL, FIFO_SIZE);
    if (ret) {
        pr_err("Failed to initialize RX FIFO\n");
        goto err_gpio;
    }
    
    /* Initialize mutexes */
    mutex_init(&dev->tx_mutex);
    mutex_init(&dev->rx_mutex);
    
    /* Register character device */
    cdev_init(&dev->cdev, &bbuart_fops);
    dev->cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&dev->cdev, MKDEV(bbuart_major, minor), 1);
    if (ret) {
        pr_err("Failed to add character device\n");
        goto err_gpio;
    }
    
    /* Create device node */
    if (!device_create(bbuart_class, NULL, MKDEV(bbuart_major, minor), 
                       NULL, DEVICE_NAME "%d", minor)) {
        pr_err("Failed to create device\n");
        ret = -ENODEV;
        goto err_cdev;
    }
    
    /* Start RX timer to begin monitoring for incoming data */
    hrtimer_start(&dev->rx_timer, ktime_set(0, 0), HRTIMER_MODE_REL);
    
    return 0;
    
err_cdev:
    cdev_del(&dev->cdev);
    
err_gpio:
    gpio_free(tx_pin);
    gpio_free(rx_pin);
    
    return ret;
}

/* Cleanup a single UART device */
static void bbuart_cleanup_device(struct bbuart_dev *dev)
{
    /* Cancel timers */
    hrtimer_cancel(&dev->tx_timer);
    hrtimer_cancel(&dev->rx_timer);
    
    /* Remove device and character device */
    device_destroy(bbuart_class, MKDEV(bbuart_major, dev->minor));
    cdev_del(&dev->cdev);
    
    /* Free GPIO pins */
    gpio_free(dev->tx_pin);
    gpio_free(dev->rx_pin);
    
    /* Free memory */
    kfree(dev);
}

/* Module parameters */
static unsigned int tx_pin = 17; /* Default TX GPIO pin */
static unsigned int rx_pin = 18; /* Default RX GPIO pin */

module_param(tx_pin, uint, S_IRUGO);
MODULE_PARM_DESC(tx_pin, "GPIO pin for TX");

module_param(rx_pin, uint, S_IRUGO);
MODULE_PARM_DESC(rx_pin, "GPIO pin for RX");

/* Module initialization */
static int __init bbuart_init(void)
{
    int ret;
    dev_t dev;
    
    /* Allocate device number range */
    ret = alloc_chrdev_region(&dev, 0, MAX_DEVICES, DEVICE_NAME);
    if (ret < 0) {
        pr_err("Failed to allocate device numbers\n");
        return ret;
    }
    
    bbuart_major = MAJOR(dev);
    
    /* Create device class */
    bbuart_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(bbuart_class)) {
        pr_err("Failed to create device class\n");
        ret = PTR_ERR(bbuart_class);
        goto err_class;
    }
    
    /* Create first UART device */
    bbuart_devices[0] = kmalloc(sizeof(struct bbuart_dev), GFP_KERNEL);
    if (!bbuart_devices[0]) {
        pr_err("Failed to allocate memory for device\n");
        ret = -ENOMEM;
        goto err_device;
    }
    
    ret = bbuart_init_device(bbuart_devices[0], 0, tx_pin, rx_pin);
    if (ret)
        goto err_init;
    
    pr_info("Bit-banged UART driver initialized on GPIO TX:%d, RX:%d\n", tx_pin, rx_pin);
    return 0;
    
err_init:
    kfree(bbuart_devices[0]);
    
err_device:
    class_destroy(bbuart_class);
    
err_class:
    unregister_chrdev_region(MKDEV(bbuart_major, 0), MAX_DEVICES);
    
    return ret;
}

/* Module cleanup */
static void __exit bbuart_exit(void)
{
    int i;
    
    /* Cleanup all devices */
    for (i = 0; i < MAX_DEVICES; i++) {
        if (bbuart_devices[i])
            bbuart_cleanup_device(bbuart_devices[i]);
    }
    
    /* Destroy class and unregister device numbers */
    class_destroy(bbuart_class);
    unregister_chrdev_region(MKDEV(bbuart_major, 0), MAX_DEVICES);
    
    pr_info("Bit-banged UART driver removed\n");
}

module_init(bbuart_init);
module_exit(bbuart_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("Bit-banged UART driver using hrtimer and GPIO");
MODULE_VERSION("1.0");
