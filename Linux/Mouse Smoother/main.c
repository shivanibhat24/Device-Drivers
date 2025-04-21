/*
 * mouse_smoother.c - Kernel Mouse Smoother
 * 
 * A Linux kernel module that smooths mouse cursor movements
 * using a low-pass filter algorithm.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("Mouse movement smoother using low-pass filter");
MODULE_VERSION("1.0");

/* Module parameters */
static int filter_strength = 4;  /* Higher values = more smoothing (1-10) */
module_param(filter_strength, int, 0644);
MODULE_PARM_DESC(filter_strength, "Smoothing strength (1-10)");

static bool enabled = true;
module_param(enabled, bool, 0644);
MODULE_PARM_DESC(enabled, "Enable/disable smoothing (1=enabled, 0=disabled)");

/* Filter state */
struct mouse_filter_data {
    int prev_x;
    int prev_y;
    int smoothed_x;
    int smoothed_y;
    bool initialized;
};

/* Input device handler */
struct input_handler smoother_handler;

/* Helper function to apply low-pass filter */
static void apply_filter(struct mouse_filter_data *filter, int *x, int *y)
{
    int alpha, beta;
    
    /* Initialize filter if this is the first event */
    if (!filter->initialized) {
        filter->prev_x = *x;
        filter->prev_y = *y;
        filter->smoothed_x = *x;
        filter->smoothed_y = *y;
        filter->initialized = true;
        return;
    }
    
    /* Calculate filter coefficients based on strength */
    /* 1 = least smoothing, 10 = most smoothing */
    alpha = 100 - (filter_strength * 8);  /* 20-92% */
    beta = 100 - alpha;
    
    /* Apply filter: new_pos = (alpha * current_pos + beta * prev_pos) / 100 */
    filter->smoothed_x = (alpha * *x + beta * filter->smoothed_x) / 100;
    filter->smoothed_y = (alpha * *y + beta * filter->smoothed_y) / 100;
    
    /* Update values */
    filter->prev_x = *x;
    filter->prev_y = *y;
    *x = filter->smoothed_x;
    *y = filter->smoothed_y;
}

/* Event handler */
static void smoother_event(struct input_handle *handle, unsigned int type, 
                          unsigned int code, int value)
{
    struct mouse_filter_data *filter_data = handle->private;
    struct input_dev *dev = handle->dev;
    int x = 0, y = 0;
    
    /* Only process relative movements */
    if (type != EV_REL)
        return;
        
    /* Skip processing if disabled */
    if (!enabled) {
        /* Pass through the original event */
        input_event(dev, type, code, value);
        return;
    }
    
    /* Accumulate x/y movements */
    if (code == REL_X)
        x = value;
    else if (code == REL_Y)
        y = value;
    else {
        /* Pass through other events */
        input_event(dev, type, code, value);
        return;
    }
    
    /* Apply filter */
    apply_filter(filter_data, &x, &y);
    
    /* Send filtered events */
    if (code == REL_X)
        input_event(dev, type, code, x);
    else if (code == REL_Y)
        input_event(dev, type, code, y);
}

/* Connect to input device */
static int smoother_connect(struct input_handler *handler, struct input_dev *dev,
                           const struct input_device_id *id)
{
    struct input_handle *handle;
    struct mouse_filter_data *filter_data;
    int error;
    
    /* Only process mouse devices */
    if (!test_bit(EV_REL, dev->evbit) || 
        !test_bit(REL_X, dev->relbit) ||
        !test_bit(REL_Y, dev->relbit))
        return -ENODEV;
    
    /* Allocate handle */
    handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
    if (!handle)
        return -ENOMEM;
    
    /* Allocate filter data */
    filter_data = kzalloc(sizeof(struct mouse_filter_data), GFP_KERNEL);
    if (!filter_data) {
        kfree(handle);
        return -ENOMEM;
    }
    
    /* Initialize filter data */
    filter_data->initialized = false;
    
    /* Set up handle */
    handle->dev = dev;
    handle->handler = handler;
    handle->name = "mouse-smoother";
    handle->private = filter_data;
    
    /* Connect to device */
    error = input_register_handle(handle);
    if (error) {
        kfree(filter_data);
        kfree(handle);
        return error;
    }
    
    error = input_open_device(handle);
    if (error) {
        input_unregister_handle(handle);
        kfree(filter_data);
        kfree(handle);
        return error;
    }
    
    printk(KERN_INFO "mouse_smoother: connected to %s\n", dev->name);
    return 0;
}

/* Disconnect from input device */
static void smoother_disconnect(struct input_handle *handle)
{
    struct mouse_filter_data *filter_data = handle->private;
    
    printk(KERN_INFO "mouse_smoother: disconnected from %s\n", handle->dev->name);
    
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(filter_data);
    kfree(handle);
}

/* Device matching */
static const struct input_device_id smoother_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_RELBIT,
        .evbit = { BIT_MASK(EV_REL) },
        .relbit = { BIT_MASK(REL_X) | BIT_MASK(REL_Y) },
    },
    { }, /* Terminating entry */
};
MODULE_DEVICE_TABLE(input, smoother_ids);

/* Input handler structure */
static struct input_handler smoother_handler = {
    .event = smoother_event,
    .connect = smoother_connect,
    .disconnect = smoother_disconnect,
    .name = "mouse_smoother",
    .id_table = smoother_ids,
};

/* Module initialization */
static int __init smoother_init(void)
{
    int ret;
    
    /* Check parameters */
    if (filter_strength < 1 || filter_strength > 10) {
        printk(KERN_WARNING "mouse_smoother: filter_strength must be between 1-10, using default (4)\n");
        filter_strength = 4;
    }
    
    /* Register input handler */
    ret = input_register_handler(&smoother_handler);
    if (ret) {
        printk(KERN_ERR "mouse_smoother: failed to register input handler\n");
        return ret;
    }
    
    printk(KERN_INFO "mouse_smoother: loaded with filter_strength=%d\n", filter_strength);
    return 0;
}

/* Module cleanup */
static void __exit smoother_exit(void)
{
    input_unregister_handler(&smoother_handler);
    printk(KERN_INFO "mouse_smoother: unloaded\n");
}

module_init(smoother_init);
module_exit(smoother_exit);
