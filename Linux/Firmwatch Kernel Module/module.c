#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/idr.h>
#include <linux/firmware.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define DEVICE_NAME "firmwatch"
#define CLASS_NAME "firmwatch"
#define MAX_FIRMWARE_SIZE (16 * 1024 * 1024) // 16MB max
#define MAX_FIRMWARE_SLOTS 256

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("Hot-reloadable firmware blob manager");
MODULE_VERSION("1.0");

// IOCTL commands
#define FIRMWATCH_IOC_MAGIC 'F'
#define FIRMWATCH_LOAD_FIRMWARE    _IOW(FIRMWATCH_IOC_MAGIC, 1, struct firmware_load_req)
#define FIRMWATCH_UNLOAD_FIRMWARE  _IOW(FIRMWATCH_IOC_MAGIC, 2, int)
#define FIRMWATCH_LIST_FIRMWARE    _IOR(FIRMWATCH_IOC_MAGIC, 3, struct firmware_list)
#define FIRMWATCH_GET_INFO         _IOWR(FIRMWATCH_IOC_MAGIC, 4, struct firmware_info)

struct firmware_load_req {
    char name[256];
    size_t size;
    int slot_id;
};

struct firmware_info {
    int slot_id;
    char name[256];
    size_t size;
    unsigned long load_time;
    int ref_count;
};

struct firmware_list {
    int count;
    struct firmware_info entries[MAX_FIRMWARE_SLOTS];
};

struct firmware_slot {
    int id;
    char name[256];
    void *data;
    size_t size;
    unsigned long load_time;
    atomic_t ref_count;
    struct mutex lock;
    bool active;
};

static int major_number;
static struct class *firmwatch_class = NULL;
static struct device *firmwatch_device = NULL;
static struct cdev firmwatch_cdev;
static dev_t dev_num;

static struct firmware_slot firmware_slots[MAX_FIRMWARE_SLOTS];
static DEFINE_IDR(slot_idr);
static DEFINE_MUTEX(slots_mutex);
static struct proc_dir_entry *proc_entry;

// Function prototypes
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static long device_ioctl(struct file *, unsigned int, unsigned long);
static int device_mmap(struct file *, struct vm_area_struct *);
static int proc_show(struct seq_file *m, void *v);
static int proc_open(struct inode *inode, struct file *file);

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_release,
    .unlocked_ioctl = device_ioctl,
    .mmap = device_mmap,
};

static const struct proc_ops proc_fops = {
    .proc_open = proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static void vm_open(struct vm_area_struct *vma)
{
    struct firmware_slot *slot = vma->vm_private_data;
    if (slot) {
        atomic_inc(&slot->ref_count);
        pr_info("firmwatch: mmap opened for slot %d, ref_count: %d\n", 
                slot->id, atomic_read(&slot->ref_count));
    }
}

static void vm_close(struct vm_area_struct *vma)
{
    struct firmware_slot *slot = vma->vm_private_data;
    if (slot) {
        atomic_dec(&slot->ref_count);
        pr_info("firmwatch: mmap closed for slot %d, ref_count: %d\n", 
                slot->id, atomic_read(&slot->ref_count));
    }
}

static vm_fault_t vm_fault(struct vm_fault *vmf)
{
    struct vm_area_struct *vma = vmf->vma;
    struct firmware_slot *slot = vma->vm_private_data;
    struct page *page;
    unsigned long offset;
    
    if (!slot || !slot->active) {
        return VM_FAULT_SIGBUS;
    }
    
    offset = vmf->pgoff << PAGE_SHIFT;
    if (offset >= slot->size) {
        return VM_FAULT_SIGBUS;
    }
    
    page = virt_to_page((char *)slot->data + offset);
    get_page(page);
    vmf->page = page;
    
    return 0;
}

static struct vm_operations_struct vm_ops = {
    .open = vm_open,
    .close = vm_close,
    .fault = vm_fault,
};

static int device_open(struct inode *inodep, struct file *filep)
{
    pr_info("firmwatch: Device opened\n");
    return 0;
}

static int device_release(struct inode *inodep, struct file *filep)
{
    pr_info("firmwatch: Device closed\n");
    return 0;
}

static int load_firmware_blob(struct firmware_load_req *req)
{
    struct firmware_slot *slot;
    const struct firmware *fw;
    int ret, slot_id;
    
    if (req->size > MAX_FIRMWARE_SIZE) {
        return -EINVAL;
    }
    
    // Request firmware from the kernel firmware loader
    ret = request_firmware(&fw, req->name, firmwatch_device);
    if (ret) {
        pr_err("firmwatch: Failed to load firmware %s: %d\n", req->name, ret);
        return ret;
    }
    
    mutex_lock(&slots_mutex);
    
    // Find or allocate a slot
    if (req->slot_id >= 0 && req->slot_id < MAX_FIRMWARE_SLOTS) {
        slot_id = req->slot_id;
        slot = &firmware_slots[slot_id];
        
        // If slot is in use, free old data
        if (slot->active) {
            vfree(slot->data);
            slot->active = false;
        }
    } else {
        // Auto-allocate slot
        slot_id = idr_alloc(&slot_idr, NULL, 0, MAX_FIRMWARE_SLOTS, GFP_KERNEL);
        if (slot_id < 0) {
            mutex_unlock(&slots_mutex);
            release_firmware(fw);
            return slot_id;
        }
        slot = &firmware_slots[slot_id];
    }
    
    // Allocate memory for firmware data
    slot->data = vmalloc(fw->size);
    if (!slot->data) {
        if (req->slot_id < 0) {
            idr_remove(&slot_idr, slot_id);
        }
        mutex_unlock(&slots_mutex);
        release_firmware(fw);
        return -ENOMEM;
    }
    
    // Copy firmware data
    memcpy(slot->data, fw->data, fw->size);
    slot->id = slot_id;
    strncpy(slot->name, req->name, sizeof(slot->name) - 1);
    slot->name[sizeof(slot->name) - 1] = '\0';
    slot->size = fw->size;
    slot->load_time = jiffies;
    atomic_set(&slot->ref_count, 0);
    mutex_init(&slot->lock);
    slot->active = true;
    
    req->slot_id = slot_id; // Return assigned slot ID
    
    mutex_unlock(&slots_mutex);
    release_firmware(fw);
    
    pr_info("firmwatch: Loaded firmware %s into slot %d (%zu bytes)\n", 
            req->name, slot_id, fw->size);
    
    return 0;
}

static int unload_firmware_blob(int slot_id)
{
    struct firmware_slot *slot;
    
    if (slot_id < 0 || slot_id >= MAX_FIRMWARE_SLOTS) {
        return -EINVAL;
    }
    
    mutex_lock(&slots_mutex);
    
    slot = &firmware_slots[slot_id];
    if (!slot->active) {
        mutex_unlock(&slots_mutex);
        return -ENOENT;
    }
    
    // Check if anyone is still using this firmware
    if (atomic_read(&slot->ref_count) > 0) {
        mutex_unlock(&slots_mutex);
        return -EBUSY;
    }
    
    vfree(slot->data);
    slot->data = NULL;
    slot->active = false;
    idr_remove(&slot_idr, slot_id);
    
    mutex_unlock(&slots_mutex);
    
    pr_info("firmwatch: Unloaded firmware from slot %d\n", slot_id);
    return 0;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    
    switch (cmd) {
    case FIRMWATCH_LOAD_FIRMWARE: {
        struct firmware_load_req req;
        if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
            return -EFAULT;
        }
        ret = load_firmware_blob(&req);
        if (ret == 0) {
            if (copy_to_user((void __user *)arg, &req, sizeof(req))) {
                return -EFAULT;
            }
        }
        break;
    }
    
    case FIRMWATCH_UNLOAD_FIRMWARE: {
        int slot_id;
        if (copy_from_user(&slot_id, (void __user *)arg, sizeof(slot_id))) {
            return -EFAULT;
        }
        ret = unload_firmware_blob(slot_id);
        break;
    }
    
    case FIRMWATCH_GET_INFO: {
        struct firmware_info info;
        struct firmware_slot *slot;
        
        if (copy_from_user(&info, (void __user *)arg, sizeof(info))) {
            return -EFAULT;
        }
        
        if (info.slot_id < 0 || info.slot_id >= MAX_FIRMWARE_SLOTS) {
            return -EINVAL;
        }
        
        mutex_lock(&slots_mutex);
        slot = &firmware_slots[info.slot_id];
        
        if (!slot->active) {
            mutex_unlock(&slots_mutex);
            return -ENOENT;
        }
        
        strncpy(info.name, slot->name, sizeof(info.name) - 1);
        info.name[sizeof(info.name) - 1] = '\0';
        info.size = slot->size;
        info.load_time = slot->load_time;
        info.ref_count = atomic_read(&slot->ref_count);
        
        mutex_unlock(&slots_mutex);
        
        if (copy_to_user((void __user *)arg, &info, sizeof(info))) {
            return -EFAULT;
        }
        break;
    }
    
    default:
        ret = -ENOTTY;
        break;
    }
    
    return ret;
}

static int device_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;
    int slot_id = vma->vm_pgoff; // Use page offset as slot ID
    struct firmware_slot *slot;
    
    if (slot_id < 0 || slot_id >= MAX_FIRMWARE_SLOTS) {
        return -EINVAL;
    }
    
    mutex_lock(&slots_mutex);
    slot = &firmware_slots[slot_id];
    
    if (!slot->active) {
        mutex_unlock(&slots_mutex);
        return -ENOENT;
    }
    
    if (size > slot->size) {
        mutex_unlock(&slots_mutex);
        return -EINVAL;
    }
    
    vma->vm_ops = &vm_ops;
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
    vma->vm_private_data = slot;
    
    atomic_inc(&slot->ref_count);
    mutex_unlock(&slots_mutex);
    
    pr_info("firmwatch: mmap for slot %d, size %lu\n", slot_id, size);
    return 0;
}

static int proc_show(struct seq_file *m, void *v)
{
    int i;
    
    seq_printf(m, "FirmWatch Status\n");
    seq_printf(m, "================\n\n");
    seq_printf(m, "%-4s %-32s %-12s %-8s %s\n", "Slot", "Name", "Size", "RefCount", "Load Time");
    seq_printf(m, "%-4s %-32s %-12s %-8s %s\n", "----", "----", "----", "--------", "---------");
    
    mutex_lock(&slots_mutex);
    for (i = 0; i < MAX_FIRMWARE_SLOTS; i++) {
        struct firmware_slot *slot = &firmware_slots[i];
        if (slot->active) {
            seq_printf(m, "%-4d %-32s %-12zu %-8d %lu\n", 
                      slot->id, slot->name, slot->size, 
                      atomic_read(&slot->ref_count), slot->load_time);
        }
    }
    mutex_unlock(&slots_mutex);
    
    return 0;
}

static int proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_show, NULL);
}

static int __init firmwatch_init(void)
{
    int ret;
    
    pr_info("firmwatch: Initializing FirmWatch module\n");
    
    // Allocate major number
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("firmwatch: Failed to allocate major number\n");
        return ret;
    }
    major_number = MAJOR(dev_num);
    
    // Initialize cdev
    cdev_init(&firmwatch_cdev, &fops);
    ret = cdev_add(&firmwatch_cdev, dev_num, 1);
    if (ret < 0) {
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }
    
    // Create device class
    firmwatch_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(firmwatch_class)) {
        cdev_del(&firmwatch_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(firmwatch_class);
    }
    
    // Create device
    firmwatch_device = device_create(firmwatch_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(firmwatch_device)) {
        class_destroy(firmwatch_class);
        cdev_del(&firmwatch_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(firmwatch_device);
    }
    
    // Initialize firmware slots
    memset(firmware_slots, 0, sizeof(firmware_slots));
    
    // Create proc entry
    proc_entry = proc_create("firmwatch", 0444, NULL, &proc_fops);
    if (!proc_entry) {
        pr_warn("firmwatch: Failed to create proc entry\n");
    }
    
    pr_info("firmwatch: Device created successfully (major: %d)\n", major_number);
    return 0;
}

static void __exit firmwatch_exit(void)
{
    int i;
    
    pr_info("firmwatch: Cleaning up FirmWatch module\n");
    
    // Remove proc entry
    if (proc_entry) {
        proc_remove(proc_entry);
    }
    
    // Clean up firmware slots
    mutex_lock(&slots_mutex);
    for (i = 0; i < MAX_FIRMWARE_SLOTS; i++) {
        struct firmware_slot *slot = &firmware_slots[i];
        if (slot->active) {
            vfree(slot->data);
            slot->active = false;
        }
    }
    idr_destroy(&slot_idr);
    mutex_unlock(&slots_mutex);
    
    // Clean up device
    device_destroy(firmwatch_class, dev_num);
    class_destroy(firmwatch_class);
    cdev_del(&firmwatch_cdev);
    unregister_chrdev_region(dev_num, 1);
    
    pr_info("firmwatch: Module cleanup complete\n");
}

module_init(firmwatch_init);
module_exit(firmwatch_exit);
