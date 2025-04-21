/*
 * keyboard_turing.c - Keyboard Turing Machine Device Driver
 *
 * This driver transforms a keyboard into a simple Turing machine.
 * Each key maps to a Turing machine operation, and the kernel
 * parses and executes the program as the user types.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("Keyboard Turing Machine Device Driver");
MODULE_VERSION("0.1");

#define DEVICE_NAME "keyboard_turing"
#define CLASS_NAME "kturing"
#define TAPE_SIZE 30000
#define BUFFER_SIZE 4096

/* Device variables */
static int major_number;
static struct class *kturing_class = NULL;
static struct device *kturing_device = NULL;
static struct cdev kturing_cdev;

/* Turing machine variables */
static unsigned char *tape = NULL;
static int pointer = 0;
static char *program_buffer = NULL;
static size_t program_length = 0;
static size_t program_position = 0;
static int execute_mode = 0;

/* Input device handling */
static struct input_handler kturing_handler;
static struct input_handle *keyboard_handle = NULL;

/* File operations */
static int kturing_open(struct inode *inode, struct file *file);
static int kturing_release(struct inode *inode, struct file *file);
static ssize_t kturing_read(struct file *file, char __user *buf, size_t count, loff_t *offset);
static ssize_t kturing_write(struct file *file, const char __user *buf, size_t count, loff_t *offset);

static struct file_operations fops = {
    .open = kturing_open,
    .release = kturing_release,
    .read = kturing_read,
    .write = kturing_write,
};

/* Initialize Turing machine state */
static int initialize_turing_machine(void) {
    tape = kmalloc(TAPE_SIZE, GFP_KERNEL);
    if (!tape) {
        printk(KERN_ALERT "Failed to allocate memory for Turing machine tape\n");
        return -ENOMEM;
    }
    memset(tape, 0, TAPE_SIZE);
    
    program_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!program_buffer) {
        printk(KERN_ALERT "Failed to allocate memory for program buffer\n");
        kfree(tape);
        return -ENOMEM;
    }
    memset(program_buffer, 0, BUFFER_SIZE);
    
    pointer = 0;
    program_length = 0;
    program_position = 0;
    execute_mode = 0;
    
    return 0;
}

/* Cleanup Turing machine state */
static void cleanup_turing_machine(void) {
    if (tape) {
        kfree(tape);
        tape = NULL;
    }
    
    if (program_buffer) {
        kfree(program_buffer);
        program_buffer = NULL;
    }
}

/* Execute a single Turing machine instruction */
static void execute_instruction(char instruction) {
    switch (instruction) {
        case '>':
            pointer = (pointer + 1) % TAPE_SIZE;
            break;
        case '<':
            pointer = (pointer - 1 + TAPE_SIZE) % TAPE_SIZE;
            break;
        case '+':
            tape[pointer]++;
            break;
        case '-':
            tape[pointer]--;
            break;
        case '.':
            printk(KERN_INFO "Output: %c (%d)\n", tape[pointer], tape[pointer]);
            break;
        case ',':
            /* Input operation would require more complex handling */
            printk(KERN_INFO "Input operation not fully implemented\n");
            break;
        case '[':
            if (tape[pointer] == 0) {
                int loop_depth = 1;
                while (loop_depth > 0 && program_position < program_length) {
                    program_position++;
                    if (program_buffer[program_position] == '[')
                        loop_depth++;
                    else if (program_buffer[program_position] == ']')
                        loop_depth--;
                }
            }
            break;
        case ']':
            if (tape[pointer] != 0) {
                int loop_depth = 1;
                while (loop_depth > 0 && program_position > 0) {
                    program_position--;
                    if (program_buffer[program_position] == ']')
                        loop_depth++;
                    else if (program_buffer[program_position] == '[')
                        loop_depth--;
                }
            }
            break;
        default:
            /* Ignore other characters */
            break;
    }
}

/* Process key events */
static void handle_key_event(unsigned int key_code, int value) {
    char instruction = 0;
    
    /* Process only key press events (value == 1) */
    if (value != 1)
        return;
    
    /* Map key codes to Turing machine instructions */
    switch (key_code) {
        case KEY_RIGHT:
        case KEY_D:
            instruction = '>';
            break;
        case KEY_LEFT:
        case KEY_A:
            instruction = '<';
            break;
        case KEY_UP:
        case KEY_W:
            instruction = '+';
            break;
        case KEY_DOWN:
        case KEY_S:
            instruction = '-';
            break;
        case KEY_DOT:
        case KEY_P:
            instruction = '.';
            break;
        case KEY_COMMA:
        case KEY_I:
            instruction = ',';
            break;
        case KEY_LEFTBRACE:
        case KEY_O:
            instruction = '[';
            break;
        case KEY_RIGHTBRACE:
        case KEY_P:
            instruction = ']';
            break;
        case KEY_ENTER:
            /* Toggle execute mode */
            execute_mode = !execute_mode;
            if (execute_mode) {
                printk(KERN_INFO "Executing Turing machine program\n");
                program_position = 0;
            } else {
                printk(KERN_INFO "Stopped execution, back to program mode\n");
            }
            return;
        case KEY_ESC:
            /* Reset Turing machine */
            memset(tape, 0, TAPE_SIZE);
            pointer = 0;
            program_position = 0;
            printk(KERN_INFO "Turing machine reset\n");
            return;
        default:
            return; /* Ignore other keys */
    }
    
    if (execute_mode) {
        /* In execute mode, directly process the instruction */
        execute_instruction(instruction);
        printk(KERN_INFO "Executed: %c, Pointer: %d, Value: %d\n", 
               instruction, pointer, tape[pointer]);
    } else {
        /* In program mode, add instruction to the buffer */
        if (program_length < BUFFER_SIZE - 1) {
            program_buffer[program_length++] = instruction;
            program_buffer[program_length] = '\0';
            printk(KERN_INFO "Added instruction: %c, Program length: %zu\n", 
                   instruction, program_length);
        } else {
            printk(KERN_WARNING "Program buffer full\n");
        }
    }
}

/* Input event callback */
static bool kturing_event(struct input_handle *handle, unsigned int type, 
                          unsigned int code, int value) {
    if (type == EV_KEY) {
        handle_key_event(code, value);
    }
    return false;
}

/* Connect callback */
static int kturing_connect(struct input_handler *handler, struct input_dev *dev,
                          const struct input_device_id *id) {
    struct input_handle *handle;
    int error;
    
    /* Only connect to keyboards */
    if (!(dev->evbit[0] & BIT(EV_KEY)))
        return -ENODEV;
    
    handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
    if (!handle)
        return -ENOMEM;
    
    handle->dev = dev;
    handle->handler = handler;
    handle->name = "keyboard_turing";
    
    error = input_register_handle(handle);
    if (error)
        goto err_free_handle;
    
    error = input_open_device(handle);
    if (error)
        goto err_unregister_handle;
    
    keyboard_handle = handle;
    printk(KERN_INFO "Connected to keyboard: %s\n", dev->name);
    
    return 0;
    
err_unregister_handle:
    input_unregister_handle(handle);
err_free_handle:
    kfree(handle);
    return error;
}

/* Disconnect callback */
static void kturing_disconnect(struct input_handle *handle) {
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(handle);
    
    if (handle == keyboard_handle)
        keyboard_handle = NULL;
    
    printk(KERN_INFO "Disconnected from keyboard\n");
}

/* Input device ID table */
static const struct input_device_id kturing_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
        .evbit = { BIT_MASK(EV_KEY) },
    },
    { },
};
MODULE_DEVICE_TABLE(input, kturing_ids);

/* File operations implementation */
static int kturing_open(struct inode *inode, struct file *file) {
    return 0;
}

static int kturing_release(struct inode *inode, struct file *file) {
    return 0;
}

static ssize_t kturing_read(struct file *file, char __user *buf, size_t count, loff_t *offset) {
    char *status_buffer;
    int len, ret;
    
    status_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!status_buffer)
        return -ENOMEM;
    
    /* Create a status string with current Turing machine state */
    len = snprintf(status_buffer, BUFFER_SIZE,
                   "Turing Machine Status:\n"
                   "Execute Mode: %s\n"
                   "Pointer Position: %d\n"
                   "Current Value: %d\n"
                   "Program Length: %zu\n"
                   "Program Position: %zu\n"
                   "Program: %s\n",
                   execute_mode ? "On" : "Off",
                   pointer,
                   tape[pointer],
                   program_length,
                   program_position,
                   program_buffer);
    
    if (*offset >= len) {
        kfree(status_buffer);
        return 0;
    }
    
    if (count > len - *offset)
        count = len - *offset;
    
    ret = copy_to_user(buf, status_buffer + *offset, count);
    if (ret) {
        kfree(status_buffer);
        return -EFAULT;
    }
    
    *offset += count;
    kfree(status_buffer);
    
    return count;
}

static ssize_t kturing_write(struct file *file, const char __user *buf, size_t count, loff_t *offset) {
    char *temp_buffer;
    size_t i;
    
    if (count > BUFFER_SIZE - 1)
        count = BUFFER_SIZE - 1;
    
    temp_buffer = kmalloc(count + 1, GFP_KERNEL);
    if (!temp_buffer)
        return -ENOMEM;
    
    if (copy_from_user(temp_buffer, buf, count)) {
        kfree(temp_buffer);
        return -EFAULT;
    }
    
    temp_buffer[count] = '\0';
    
    /* Clear the current program */
    memset(program_buffer, 0, BUFFER_SIZE);
    program_length = 0;
    
    /* Copy valid instructions to the program buffer */
    for (i = 0; i < count; i++) {
        char c = temp_buffer[i];
        if (c == '>' || c == '<' || c == '+' || c == '-' || 
            c == '.' || c == ',' || c == '[' || c == ']') {
            program_buffer[program_length++] = c;
        }
    }
    program_buffer[program_length] = '\0';
    
    kfree(temp_buffer);
    printk(KERN_INFO "Loaded program with %zu instructions\n", program_length);
    
    return count;
}

/* Input handler structure */
static struct input_handler kturing_handler = {
    .event = kturing_event,
    .connect = kturing_connect,
    .disconnect = kturing_disconnect,
    .name = "keyboard_turing",
    .id_table = kturing_ids,
};

/* Module initialization */
static int __init keyboard_turing_init(void) {
    int result;
    dev_t dev;
    
    /* Initialize the Turing machine */
    result = initialize_turing_machine();
    if (result < 0)
        return result;
    
    /* Allocate a device number */
    result = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    if (result < 0) {
        printk(KERN_ALERT "Failed to allocate device number\n");
        cleanup_turing_machine();
        return result;
    }
    major_number = MAJOR(dev);
    
    /* Create character device */
    cdev_init(&kturing_cdev, &fops);
    kturing_cdev.owner = THIS_MODULE;
    result = cdev_add(&kturing_cdev, dev, 1);
    if (result < 0) {
        printk(KERN_ALERT "Failed to add character device\n");
        unregister_chrdev_region(dev, 1);
        cleanup_turing_machine();
        return result;
    }
    
    /* Create device class */
    kturing_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(kturing_class)) {
        printk(KERN_ALERT "Failed to create device class\n");
        cdev_del(&kturing_cdev);
        unregister_chrdev_region(dev, 1);
        cleanup_turing_machine();
        return PTR_ERR(kturing_class);
    }
    
    /* Create device file */
    kturing_device = device_create(kturing_class, NULL, dev, NULL, DEVICE_NAME);
    if (IS_ERR(kturing_device)) {
        printk(KERN_ALERT "Failed to create device\n");
        class_destroy(kturing_class);
        cdev_del(&kturing_cdev);
        unregister_chrdev_region(dev, 1);
        cleanup_turing_machine();
        return PTR_ERR(kturing_device);
    }
    
    /* Register input handler */
    result = input_register_handler(&kturing_handler);
    if (result) {
        printk(KERN_ALERT "Failed to register input handler\n");
        device_destroy(kturing_class, dev);
        class_destroy(kturing_class);
        cdev_del(&kturing_cdev);
        unregister_chrdev_region(dev, 1);
        cleanup_turing_machine();
        return result;
    }
    
    printk(KERN_INFO "Keyboard Turing Machine module loaded\n");
    return 0;
}

/* Module cleanup */
static void __exit keyboard_turing_exit(void) {
    dev_t dev = MKDEV(major_number, 0);
    
    /* Unregister input handler */
    input_unregister_handler(&kturing_handler);
    
    /* Clean up device */
    device_destroy(kturing_class, dev);
    class_destroy(kturing_class);
    cdev_del(&kturing_cdev);
    unregister_chrdev_region(dev, 1);
    
    /* Clean up Turing machine */
    cleanup_turing_machine();
    
    printk(KERN_INFO "Keyboard Turing Machine module unloaded\n");
}

module_init(keyboard_turing_init);
module_exit(keyboard_turing_exit);
