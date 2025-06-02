#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/syscalls.h>
#include <linux/binfmts.h>
#include <asm/syscall.h>

#define DEVICE_NAME "trace_emitter"
#define CLASS_NAME "trace_class"
#define BUFFER_SIZE 4096
#define MAX_TRACE_ENTRIES 1000

// IOCTL commands
#define TRACE_IOC_MAGIC 't'
#define TRACE_IOC_START _IO(TRACE_IOC_MAGIC, 1)
#define TRACE_IOC_STOP _IO(TRACE_IOC_MAGIC, 2)
#define TRACE_IOC_CLEAR _IO(TRACE_IOC_MAGIC, 3)
#define TRACE_IOC_SET_PID _IOW(TRACE_IOC_MAGIC, 4, int)

// Trace event types
typedef enum {
    TRACE_SYSCALL_ENTER = 1,
    TRACE_SYSCALL_EXIT = 2,
    TRACE_EXEC = 3,
    TRACE_FORK = 4
} trace_event_type_t;

// Trace entry structure
typedef struct {
    trace_event_type_t type;
    pid_t pid;
    pid_t ppid;
    unsigned long timestamp;
    union {
        struct {
            long syscall_nr;
            unsigned long args[6];
            long retval;
        } syscall;
        struct {
            char filename[256];
            char args[512];
        } exec;
    } data;
} trace_entry_t;

// Circular buffer for trace entries
typedef struct {
    trace_entry_t *entries;
    int head;
    int tail;
    int count;
    int max_entries;
    struct mutex lock;
    wait_queue_head_t wait_queue;
} trace_buffer_t;

// Driver state
static struct {
    int major_number;
    struct class *device_class;
    struct device *device;
    struct cdev cdev;
    trace_buffer_t buffer;
    bool tracing_enabled;
    pid_t target_pid;  // 0 means trace all processes
    struct mutex state_lock;
} driver_state;

// Kprobe structures
static struct kprobe kp_sys_enter;
static struct kprobe kp_sys_exit;
static struct kprobe kp_exec;
static struct kprobe kp_fork;

// Function prototypes
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);
static long device_ioctl(struct file *, unsigned int, unsigned long);

// File operations structure
static struct file_operations fops = {
    .open = device_open,
    .read = device_read,
    .write = device_write,
    .release = device_release,
    .unlocked_ioctl = device_ioctl,
};

// Initialize trace buffer
static int init_trace_buffer(trace_buffer_t *buf, int max_entries)
{
    buf->entries = kzalloc(sizeof(trace_entry_t) * max_entries, GFP_KERNEL);
    if (!buf->entries)
        return -ENOMEM;
    
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
    buf->max_entries = max_entries;
    mutex_init(&buf->lock);
    init_waitqueue_head(&buf->wait_queue);
    
    return 0;
}

// Cleanup trace buffer
static void cleanup_trace_buffer(trace_buffer_t *buf)
{
    if (buf->entries) {
        kfree(buf->entries);
        buf->entries = NULL;
    }
}

// Add entry to trace buffer
static void add_trace_entry(trace_buffer_t *buf, const trace_entry_t *entry)
{
    mutex_lock(&buf->lock);
    
    // Copy entry to buffer
    memcpy(&buf->entries[buf->head], entry, sizeof(trace_entry_t));
    
    // Update head pointer
    buf->head = (buf->head + 1) % buf->max_entries;
    
    // Handle buffer overflow
    if (buf->count < buf->max_entries) {
        buf->count++;
    } else {
        // Buffer full, advance tail
        buf->tail = (buf->tail + 1) % buf->max_entries;
    }
    
    mutex_unlock(&buf->lock);
    
    // Wake up waiting readers
    wake_up_interruptible(&buf->wait_queue);
}

// Get current timestamp
static unsigned long get_timestamp(void)
{
    struct timespec64 ts;
    ktime_get_real_ts64(&ts);
    return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// Syscall enter probe handler
static int syscall_enter_handler(struct kprobe *p, struct pt_regs *regs)
{
    trace_entry_t entry;
    struct task_struct *task = current;
    
    if (!driver_state.tracing_enabled)
        return 0;
    
    // Filter by PID if specified
    if (driver_state.target_pid != 0 && task->pid != driver_state.target_pid)
        return 0;
    
    memset(&entry, 0, sizeof(entry));
    entry.type = TRACE_SYSCALL_ENTER;
    entry.pid = task->pid;
    entry.ppid = task->parent->pid;
    entry.timestamp = get_timestamp();
    entry.data.syscall.syscall_nr = syscall_get_nr(task, regs);
    
    // Get syscall arguments
    syscall_get_arguments(task, regs, entry.data.syscall.args);
    
    add_trace_entry(&driver_state.buffer, &entry);
    return 0;
}

// Syscall exit probe handler
static int syscall_exit_handler(struct kprobe *p, struct pt_regs *regs)
{
    trace_entry_t entry;
    struct task_struct *task = current;
    
    if (!driver_state.tracing_enabled)
        return 0;
    
    // Filter by PID if specified
    if (driver_state.target_pid != 0 && task->pid != driver_state.target_pid)
        return 0;
    
    memset(&entry, 0, sizeof(entry));
    entry.type = TRACE_SYSCALL_EXIT;
    entry.pid = task->pid;
    entry.ppid = task->parent->pid;
    entry.timestamp = get_timestamp();
    entry.data.syscall.syscall_nr = syscall_get_nr(task, regs);
    entry.data.syscall.retval = regs_return_value(regs);
    
    add_trace_entry(&driver_state.buffer, &entry);
    return 0;
}

// Exec probe handler
static int exec_handler(struct kprobe *p, struct pt_regs *regs)
{
    trace_entry_t entry;
    struct task_struct *task = current;
    struct linux_binprm *bprm;
    
    if (!driver_state.tracing_enabled)
        return 0;
    
    // Filter by PID if specified
    if (driver_state.target_pid != 0 && task->pid != driver_state.target_pid)
        return 0;
    
    // Get binary format structure from first argument
    bprm = (struct linux_binprm *)regs->di;
    if (!bprm || !bprm->filename)
        return 0;
    
    memset(&entry, 0, sizeof(entry));
    entry.type = TRACE_EXEC;
    entry.pid = task->pid;
    entry.ppid = task->parent->pid;
    entry.timestamp = get_timestamp();
    
    // Copy filename
    strncpy(entry.data.exec.filename, bprm->filename, sizeof(entry.data.exec.filename) - 1);
    
    // Copy arguments if available
    if (bprm->argc > 0 && bprm->argv) {
        int i;
        char *arg_ptr = entry.data.exec.args;
        int remaining = sizeof(entry.data.exec.args) - 1;
        
        for (i = 0; i < bprm->argc && remaining > 0; i++) {
            if (i > 0) {
                *arg_ptr++ = ' ';
                remaining--;
            }
            // This is simplified - in reality you'd need to safely copy from user space
            strncpy(arg_ptr, "arg", remaining);
            arg_ptr += 3;
            remaining -= 3;
        }
    }
    
    add_trace_entry(&driver_state.buffer, &entry);
    return 0;
}

// Fork probe handler
static int fork_handler(struct kprobe *p, struct pt_regs *regs)
{
    trace_entry_t entry;
    struct task_struct *task = current;
    
    if (!driver_state.tracing_enabled)
        return 0;
    
    // Filter by PID if specified
    if (driver_state.target_pid != 0 && task->pid != driver_state.target_pid)
        return 0;
    
    memset(&entry, 0, sizeof(entry));
    entry.type = TRACE_FORK;
    entry.pid = task->pid;
    entry.ppid = task->parent->pid;
    entry.timestamp = get_timestamp();
    
    add_trace_entry(&driver_state.buffer, &entry);
    return 0;
}

// Setup kprobes
static int setup_kprobes(void)
{
    int ret;
    
    // Syscall enter probe
    kp_sys_enter.symbol_name = "syscall_trace_enter";
    kp_sys_enter.pre_handler = syscall_enter_handler;
    ret = register_kprobe(&kp_sys_enter);
    if (ret < 0) {
        printk(KERN_ERR "trace_emitter: Failed to register syscall_enter kprobe: %d\n", ret);
        return ret;
    }
    
    // Syscall exit probe
    kp_sys_exit.symbol_name = "syscall_trace_exit";
    kp_sys_exit.pre_handler = syscall_exit_handler;
    ret = register_kprobe(&kp_sys_exit);
    if (ret < 0) {
        printk(KERN_ERR "trace_emitter: Failed to register syscall_exit kprobe: %d\n", ret);
        unregister_kprobe(&kp_sys_enter);
        return ret;
    }
    
    // Exec probe
    kp_exec.symbol_name = "do_execveat_common";
    kp_exec.pre_handler = exec_handler;
    ret = register_kprobe(&kp_exec);
    if (ret < 0) {
        printk(KERN_ERR "trace_emitter: Failed to register exec kprobe: %d\n", ret);
        unregister_kprobe(&kp_sys_enter);
        unregister_kprobe(&kp_sys_exit);
        return ret;
    }
    
    // Fork probe
    kp_fork.symbol_name = "_do_fork";
    kp_fork.pre_handler = fork_handler;
    ret = register_kprobe(&kp_fork);
    if (ret < 0) {
        printk(KERN_ERR "trace_emitter: Failed to register fork kprobe: %d\n", ret);
        unregister_kprobe(&kp_sys_enter);
        unregister_kprobe(&kp_sys_exit);
        unregister_kprobe(&kp_exec);
        return ret;
    }
    
    return 0;
}

// Cleanup kprobes
static void cleanup_kprobes(void)
{
    unregister_kprobe(&kp_sys_enter);
    unregister_kprobe(&kp_sys_exit);
    unregister_kprobe(&kp_exec);
    unregister_kprobe(&kp_fork);
}

// Device file operations
static int device_open(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO "trace_emitter: Device opened\n");
    return 0;
}

static int device_release(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO "trace_emitter: Device closed\n");
    return 0;
}

static ssize_t device_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    trace_entry_t entry;
    int bytes_to_copy;
    int ret;
    
    // Wait for data if buffer is empty
    if (driver_state.buffer.count == 0) {
        if (filep->f_flags & O_NONBLOCK)
            return -EAGAIN;
        
        ret = wait_event_interruptible(driver_state.buffer.wait_queue,
                                       driver_state.buffer.count > 0);
        if (ret)
            return ret;
    }
    
    // Get entry from buffer
    mutex_lock(&driver_state.buffer.lock);
    if (driver_state.buffer.count == 0) {
        mutex_unlock(&driver_state.buffer.lock);
        return 0;
    }
    
    memcpy(&entry, &driver_state.buffer.entries[driver_state.buffer.tail], sizeof(trace_entry_t));
    driver_state.buffer.tail = (driver_state.buffer.tail + 1) % driver_state.buffer.max_entries;
    driver_state.buffer.count--;
    mutex_unlock(&driver_state.buffer.lock);
    
    // Copy to user space
    bytes_to_copy = min(len, sizeof(trace_entry_t));
    if (copy_to_user(buffer, &entry, bytes_to_copy))
        return -EFAULT;
    
    return bytes_to_copy;
}

static ssize_t device_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    printk(KERN_INFO "trace_emitter: Write operation not supported\n");
    return -ENOSYS;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    
    mutex_lock(&driver_state.state_lock);
    
    switch (cmd) {
    case TRACE_IOC_START:
        driver_state.tracing_enabled = true;
        printk(KERN_INFO "trace_emitter: Tracing started\n");
        break;
        
    case TRACE_IOC_STOP:
        driver_state.tracing_enabled = false;
        printk(KERN_INFO "trace_emitter: Tracing stopped\n");
        break;
        
    case TRACE_IOC_CLEAR:
        mutex_lock(&driver_state.buffer.lock);
        driver_state.buffer.head = 0;
        driver_state.buffer.tail = 0;
        driver_state.buffer.count = 0;
        mutex_unlock(&driver_state.buffer.lock);
        printk(KERN_INFO "trace_emitter: Buffer cleared\n");
        break;
        
    case TRACE_IOC_SET_PID:
        if (copy_from_user(&driver_state.target_pid, (int *)arg, sizeof(int))) {
            ret = -EFAULT;
        } else {
            printk(KERN_INFO "trace_emitter: Target PID set to %d\n", driver_state.target_pid);
        }
        break;
        
    default:
        ret = -ENOTTY;
        break;
    }
    
    mutex_unlock(&driver_state.state_lock);
    return ret;
}

// Module initialization
static int __init trace_emitter_init(void)
{
    int ret;
    dev_t dev_num;
    
    printk(KERN_INFO "trace_emitter: Initializing module\n");
    
    // Initialize driver state
    memset(&driver_state, 0, sizeof(driver_state));
    mutex_init(&driver_state.state_lock);
    
    // Allocate character device number
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "trace_emitter: Failed to allocate device number: %d\n", ret);
        return ret;
    }
    driver_state.major_number = MAJOR(dev_num);
    
    // Initialize character device
    cdev_init(&driver_state.cdev, &fops);
    driver_state.cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&driver_state.cdev, dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "trace_emitter: Failed to add character device: %d\n", ret);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }
    
    // Create device class
    driver_state.device_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(driver_state.device_class)) {
        ret = PTR_ERR(driver_state.device_class);
        printk(KERN_ERR "trace_emitter: Failed to create device class: %d\n", ret);
        cdev_del(&driver_state.cdev);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }
    
    // Create device
    driver_state.device = device_create(driver_state.device_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(driver_state.device)) {
        ret = PTR_ERR(driver_state.device);
        printk(KERN_ERR "trace_emitter: Failed to create device: %d\n", ret);
        class_destroy(driver_state.device_class);
        cdev_del(&driver_state.cdev);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }
    
    // Initialize trace buffer
    ret = init_trace_buffer(&driver_state.buffer, MAX_TRACE_ENTRIES);
    if (ret < 0) {
        printk(KERN_ERR "trace_emitter: Failed to initialize trace buffer: %d\n", ret);
        device_destroy(driver_state.device_class, dev_num);
        class_destroy(driver_state.device_class);
        cdev_del(&driver_state.cdev);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }
    
    // Setup kprobes
    ret = setup_kprobes();
    if (ret < 0) {
        cleanup_trace_buffer(&driver_state.buffer);
        device_destroy(driver_state.device_class, dev_num);
        class_destroy(driver_state.device_class);
        cdev_del(&driver_state.cdev);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }
    
    printk(KERN_INFO "trace_emitter: Module initialized successfully\n");
    printk(KERN_INFO "trace_emitter: Device created with major number %d\n", driver_state.major_number);
    
    return 0;
}

// Module cleanup
static void __exit trace_emitter_exit(void)
{
    dev_t dev_num = MKDEV(driver_state.major_number, 0);
    
    printk(KERN_INFO "trace_emitter: Cleaning up module\n");
    
    // Stop tracing
    driver_state.tracing_enabled = false;
    
    // Cleanup kprobes
    cleanup_kprobes();
    
    // Cleanup trace buffer
    cleanup_trace_buffer(&driver_state.buffer);
    
    // Remove device and class
    device_destroy(driver_state.device_class, dev_num);
    class_destroy(driver_state.device_class);
    cdev_del(&driver_state.cdev);
    unregister_chrdev_region(dev_num, 1);
    
    printk(KERN_INFO "trace_emitter: Module cleanup complete\n");
}

module_init(trace_emitter_init);
module_exit(trace_emitter_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("Instruction Trace Emitter Device Driver");
MODULE_VERSION("1.0");
