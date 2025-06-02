/*
 * Undo Block Device Driver - Linux Kernel Module
 * Provides journaling and git-like undo functionality for block devices
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#include <linux/crc32.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>

#define DRIVER_NAME "undo_blk"
#define DEVICE_NAME "undo"
#define KERNEL_SECTOR_SIZE 512
#define DEFAULT_CAPACITY (64 * 1024 * 1024) // 64MB
#define MAX_JOURNAL_ENTRIES 1024
#define MAX_SNAPSHOTS 64
#define JOURNAL_MAGIC 0xDEADBEEF

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("Undo Block Device with Journaling Support");
MODULE_VERSION("1.0");

// Journal entry types
enum journal_type {
    JOURNAL_WRITE = 1,
    JOURNAL_SNAPSHOT = 2,
    JOURNAL_COMMIT = 3,
    JOURNAL_ROLLBACK = 4
};

// Journal entry structure
struct journal_entry {
    u32 magic;
    enum journal_type type;
    u64 timestamp;
    sector_t sector;
    u32 nr_sectors;
    u32 checksum;
    void *data;
    struct list_head list;
};

// Snapshot structure
struct snapshot_entry {
    u64 timestamp;
    u32 journal_seq;
    char description[64];
    struct list_head list;
};

// Main device structure
struct undo_device {
    int major;
    struct gendisk *gd;
    struct request_queue *queue;
    spinlock_t lock;
    struct mutex journal_mutex;
    
    // Storage
    void *data;
    sector_t capacity;
    
    // Journal system
    struct list_head journal_list;
    struct list_head snapshot_list;
    u32 journal_seq;
    u32 journal_count;
    u32 snapshot_count;
    
    // Proc interface
    struct proc_dir_entry *proc_dir;
    struct proc_dir_entry *proc_status;
    struct proc_dir_entry *proc_snapshots;
    struct proc_dir_entry *proc_journal;
    
    // Work queue for async operations
    struct workqueue_struct *work_queue;
    struct work_struct rollback_work;
    u32 rollback_target;
};

static struct undo_device *undo_dev;

// Function prototypes
static int undo_open(struct block_device *bdev, fmode_t mode);
static void undo_release(struct gendisk *gd, fmode_t mode);
static blk_qc_t undo_make_request(struct request_queue *q, struct bio *bio);
static int add_journal_entry(struct undo_device *dev, enum journal_type type,
                           sector_t sector, u32 nr_sectors, void *data);
static int create_snapshot(struct undo_device *dev, const char *description);
static int rollback_to_snapshot(struct undo_device *dev, u32 snapshot_id);

// Block device operations
static const struct block_device_operations undo_fops = {
    .owner = THIS_MODULE,
    .open = undo_open,
    .release = undo_release,
};

// Open device
static int undo_open(struct block_device *bdev, fmode_t mode)
{
    pr_info("undo_blk: Device opened\n");
    return 0;
}

// Release device
static void undo_release(struct gendisk *gd, fmode_t mode)
{
    pr_info("undo_blk: Device released\n");
}

// Calculate CRC32 checksum
static u32 calculate_checksum(const void *data, size_t len)
{
    return crc32(0, data, len);
}

// Add journal entry
static int add_journal_entry(struct undo_device *dev, enum journal_type type,
                           sector_t sector, u32 nr_sectors, void *data)
{
    struct journal_entry *entry;
    size_t data_size = nr_sectors * KERNEL_SECTOR_SIZE;
    
    if (dev->journal_count >= MAX_JOURNAL_ENTRIES) {
        pr_warn("undo_blk: Journal full, dropping oldest entry\n");
        // In a real implementation, we'd implement journal cleanup
        return -ENOSPC;
    }
    
    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry)
        return -ENOMEM;
    
    entry->magic = JOURNAL_MAGIC;
    entry->type = type;
    entry->timestamp = ktime_get_real_seconds();
    entry->sector = sector;
    entry->nr_sectors = nr_sectors;
    
    if (data && data_size > 0) {
        entry->data = vmalloc(data_size);
        if (!entry->data) {
            kfree(entry);
            return -ENOMEM;
        }
        memcpy(entry->data, data, data_size);
        entry->checksum = calculate_checksum(entry->data, data_size);
    }
    
    mutex_lock(&dev->journal_mutex);
    list_add_tail(&entry->list, &dev->journal_list);
    dev->journal_seq++;
    dev->journal_count++;
    mutex_unlock(&dev->journal_mutex);
    
    pr_debug("undo_blk: Added journal entry seq=%u, type=%d, sector=%llu\n",
             dev->journal_seq, type, (unsigned long long)sector);
    
    return 0;
}

// Create snapshot
static int create_snapshot(struct undo_device *dev, const char *description)
{
    struct snapshot_entry *snap;
    
    if (dev->snapshot_count >= MAX_SNAPSHOTS) {
        pr_warn("undo_blk: Maximum snapshots reached\n");
        return -ENOSPC;
    }
    
    snap = kzalloc(sizeof(*snap), GFP_KERNEL);
    if (!snap)
        return -ENOMEM;
    
    snap->timestamp = ktime_get_real_seconds();
    snap->journal_seq = dev->journal_seq;
    if (description)
        strncpy(snap->description, description, sizeof(snap->description) - 1);
    else
        snprintf(snap->description, sizeof(snap->description), 
                "Snapshot %u", dev->snapshot_count);
    
    mutex_lock(&dev->journal_mutex);
    list_add_tail(&snap->list, &dev->snapshot_list);
    dev->snapshot_count++;
    mutex_unlock(&dev->journal_mutex);
    
    // Add snapshot to journal
    add_journal_entry(dev, JOURNAL_SNAPSHOT, 0, 0, NULL);
    
    pr_info("undo_blk: Created snapshot '%s' at seq %u\n", 
            snap->description, snap->journal_seq);
    
    return dev->snapshot_count - 1;
}

// Rollback work function
static void rollback_work_fn(struct work_struct *work)
{
    struct undo_device *dev = container_of(work, struct undo_device, rollback_work);
    struct journal_entry *entry, *tmp;
    struct snapshot_entry *target_snap = NULL;
    u32 target_seq = dev->rollback_target;
    u32 current_id = 0;
    
    pr_info("undo_blk: Starting rollback to snapshot %u\n", target_seq);
    
    // Find target snapshot
    mutex_lock(&dev->journal_mutex);
    list_for_each_entry(target_snap, &dev->snapshot_list, list) {
        if (current_id == target_seq)
            break;
        current_id++;
    }
    
    if (current_id != target_seq) {
        pr_err("undo_blk: Snapshot %u not found\n", target_seq);
        mutex_unlock(&dev->journal_mutex);
        return;
    }
    
    pr_info("undo_blk: Rolling back to '%s' (seq %u)\n", 
            target_snap->description, target_snap->journal_seq);
    
    // Apply journal entries in reverse order
    list_for_each_entry_safe_reverse(entry, tmp, &dev->journal_list, list) {
        if (entry->type == JOURNAL_WRITE && 
            dev->journal_seq > target_snap->journal_seq) {
            
            // Restore original data
            if (entry->data) {
                void *dest = dev->data + (entry->sector * KERNEL_SECTOR_SIZE);
                memcpy(dest, entry->data, entry->nr_sectors * KERNEL_SECTOR_SIZE);
                pr_debug("undo_blk: Restored sector %llu\n", 
                        (unsigned long long)entry->sector);
            }
        }
        
        // Remove journal entries newer than target
        if (dev->journal_seq > target_snap->journal_seq) {
            list_del(&entry->list);
            if (entry->data)
                vfree(entry->data);
            kfree(entry);
            dev->journal_count--;
        }
    }
    
    dev->journal_seq = target_snap->journal_seq;
    mutex_unlock(&dev->journal_mutex);
    
    // Add rollback entry to journal
    add_journal_entry(dev, JOURNAL_ROLLBACK, 0, 0, NULL);
    
    pr_info("undo_blk: Rollback completed\n");
}

// Rollback to snapshot
static int rollback_to_snapshot(struct undo_device *dev, u32 snapshot_id)
{
    if (snapshot_id >= dev->snapshot_count) {
        pr_err("undo_blk: Invalid snapshot ID %u\n", snapshot_id);
        return -EINVAL;
    }
    
    dev->rollback_target = snapshot_id;
    queue_work(dev->work_queue, &dev->rollback_work);
    
    return 0;
}

// Handle bio requests
static blk_qc_t undo_make_request(struct request_queue *q, struct bio *bio)
{
    struct undo_device *dev = q->queuedata;
    sector_t sector = bio->bi_iter.bi_sector;
    unsigned int nr_sectors = bio_sectors(bio);
    void *buffer;
    struct bio_vec bvec;
    struct bvec_iter iter;
    char *disk_mem;
    int err = 0;
    
    // Check bounds
    if (sector + nr_sectors > dev->capacity) {
        pr_err("undo_blk: Request beyond device capacity\n");
        bio_io_error(bio);
        return BLK_QC_T_NONE;
    }
    
    disk_mem = dev->data + (sector * KERNEL_SECTOR_SIZE);
    
    // Handle read/write operations
    if (bio_data_dir(bio) == WRITE) {
        // Save original data to journal before writing
        buffer = vmalloc(nr_sectors * KERNEL_SECTOR_SIZE);
        if (buffer) {
            memcpy(buffer, disk_mem, nr_sectors * KERNEL_SECTOR_SIZE);
            add_journal_entry(dev, JOURNAL_WRITE, sector, nr_sectors, buffer);
            vfree(buffer);
        }
        
        // Perform write
        bio_for_each_segment(bvec, bio, iter) {
            void *src = kmap_atomic(bvec.bv_page);
            memcpy(disk_mem, src + bvec.bv_offset, bvec.bv_len);
            kunmap_atomic(src);
            disk_mem += bvec.bv_len;
        }
        
        pr_debug("undo_blk: Write sector=%llu, nr_sectors=%u\n",
                (unsigned long long)sector, nr_sectors);
    } else {
        // Perform read
        bio_for_each_segment(bvec, bio, iter) {
            void *dst = kmap_atomic(bvec.bv_page);
            memcpy(dst + bvec.bv_offset, disk_mem, bvec.bv_len);
            kunmap_atomic(dst);
            disk_mem += bvec.bv_len;
        }
        
        pr_debug("undo_blk: Read sector=%llu, nr_sectors=%u\n",
                (unsigned long long)sector, nr_sectors);
    }
    
    bio_endio(bio);
    return BLK_QC_T_NONE;
}

// Proc interface for status
static int proc_status_show(struct seq_file *m, void *v)
{
    struct undo_device *dev = undo_dev;
    
    seq_printf(m, "Undo Block Device Status\n");
    seq_printf(m, "========================\n");
    seq_printf(m, "Major number: %d\n", dev->major);
    seq_printf(m, "Capacity: %llu sectors (%llu MB)\n", 
               (unsigned long long)dev->capacity,
               (unsigned long long)(dev->capacity * KERNEL_SECTOR_SIZE) >> 20);
    seq_printf(m, "Journal entries: %u / %u\n", dev->journal_count, MAX_JOURNAL_ENTRIES);
    seq_printf(m, "Snapshots: %u / %u\n", dev->snapshot_count, MAX_SNAPSHOTS);
    seq_printf(m, "Current journal sequence: %u\n", dev->journal_seq);
    
    return 0;
}

static int proc_status_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_status_show, NULL);
}

static const struct proc_ops proc_status_ops = {
    .proc_open = proc_status_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

// Proc interface for snapshots
static int proc_snapshots_show(struct seq_file *m, void *v)
{
    struct undo_device *dev = undo_dev;
    struct snapshot_entry *snap;
    u32 id = 0;
    
    seq_printf(m, "ID  Timestamp           Seq    Description\n");
    seq_printf(m, "--- ------------------- ------ -----------\n");
    
    mutex_lock(&dev->journal_mutex);
    list_for_each_entry(snap, &dev->snapshot_list, list) {
        seq_printf(m, "%-3u %llu %-6u %s\n",
                  id++, snap->timestamp, snap->journal_seq, snap->description);
    }
    mutex_unlock(&dev->journal_mutex);
    
    return 0;
}

static int proc_snapshots_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_snapshots_show, NULL);
}

// Handle snapshot commands
static ssize_t proc_snapshots_write(struct file *file, const char __user *buffer,
                                   size_t count, loff_t *pos)
{
    struct undo_device *dev = undo_dev;
    char cmd[128];
    char desc[64] = "";
    u32 snap_id;
    int ret;
    
    if (count >= sizeof(cmd))
        return -EINVAL;
    
    if (copy_from_user(cmd, buffer, count))
        return -EFAULT;
    
    cmd[count] = '\0';
    
    if (sscanf(cmd, "create %63s", desc) == 1) {
        ret = create_snapshot(dev, desc);
        if (ret < 0)
            return ret;
        pr_info("undo_blk: Created snapshot '%s'\n", desc);
    } else if (sscanf(cmd, "rollback %u", &snap_id) == 1) {
        ret = rollback_to_snapshot(dev, snap_id);
        if (ret < 0)
            return ret;
        pr_info("undo_blk: Initiated rollback to snapshot %u\n", snap_id);
    } else {
        pr_err("undo_blk: Invalid command. Use 'create <desc>' or 'rollback <id>'\n");
        return -EINVAL;
    }
    
    return count;
}

static const struct proc_ops proc_snapshots_ops = {
    .proc_open = proc_snapshots_open,
    .proc_read = seq_read,
    .proc_write = proc_snapshots_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

// Proc interface for journal
static int proc_journal_show(struct seq_file *m, void *v)
{
    struct undo_device *dev = undo_dev;
    struct journal_entry *entry;
    u32 id = 0;
    const char *type_str;
    
    seq_printf(m, "ID  Type     Timestamp   Sector     Sectors Checksum\n");
    seq_printf(m, "--- -------- ----------- ---------- ------- --------\n");
    
    mutex_lock(&dev->journal_mutex);
    list_for_each_entry(entry, &dev->journal_list, list) {
        switch (entry->type) {
            case JOURNAL_WRITE: type_str = "WRITE"; break;
            case JOURNAL_SNAPSHOT: type_str = "SNAPSHOT"; break;
            case JOURNAL_COMMIT: type_str = "COMMIT"; break;
            case JOURNAL_ROLLBACK: type_str = "ROLLBACK"; break;
            default: type_str = "UNKNOWN"; break;
        }
        
        seq_printf(m, "%-3u %-8s %llu %10llu %7u %08x\n",
                  id++, type_str, entry->timestamp,
                  (unsigned long long)entry->sector,
                  entry->nr_sectors, entry->checksum);
    }
    mutex_unlock(&dev->journal_mutex);
    
    return 0;
}

static int proc_journal_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_journal_show, NULL);
}

static const struct proc_ops proc_journal_ops = {
    .proc_open = proc_journal_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

// Initialize proc interface
static int init_proc_interface(struct undo_device *dev)
{
    dev->proc_dir = proc_mkdir(DRIVER_NAME, NULL);
    if (!dev->proc_dir) {
        pr_err("undo_blk: Failed to create proc directory\n");
        return -ENOMEM;
    }
    
    dev->proc_status = proc_create("status", 0444, dev->proc_dir, &proc_status_ops);
    if (!dev->proc_status) {
        pr_err("undo_blk: Failed to create status proc entry\n");
        goto err_status;
    }
    
    dev->proc_snapshots = proc_create("snapshots", 0644, dev->proc_dir, &proc_snapshots_ops);
    if (!dev->proc_snapshots) {
        pr_err("undo_blk: Failed to create snapshots proc entry\n");
        goto err_snapshots;
    }
    
    dev->proc_journal = proc_create("journal", 0444, dev->proc_dir, &proc_journal_ops);
    if (!dev->proc_journal) {
        pr_err("undo_blk: Failed to create journal proc entry\n");
        goto err_journal;
    }
    
    return 0;
    
err_journal:
    proc_remove(dev->proc_snapshots);
err_snapshots:
    proc_remove(dev->proc_status);
err_status:
    proc_remove(dev->proc_dir);
    return -ENOMEM;
}

// Cleanup proc interface
static void cleanup_proc_interface(struct undo_device *dev)
{
    if (dev->proc_journal)
        proc_remove(dev->proc_journal);
    if (dev->proc_snapshots)
        proc_remove(dev->proc_snapshots);
    if (dev->proc_status)
        proc_remove(dev->proc_status);
    if (dev->proc_dir)
        proc_remove(dev->proc_dir);
}

// Module initialization
static int __init undo_blk_init(void)
{
    int ret;
    
    pr_info("undo_blk: Initializing Undo Block Device Driver\n");
    
    // Allocate device structure
    undo_dev = kzalloc(sizeof(*undo_dev), GFP_KERNEL);
    if (!undo_dev) {
        pr_err("undo_blk: Failed to allocate device structure\n");
        return -ENOMEM;
    }
    
    // Initialize device
    spin_lock_init(&undo_dev->lock);
    mutex_init(&undo_dev->journal_mutex);
    INIT_LIST_HEAD(&undo_dev->journal_list);
    INIT_LIST_HEAD(&undo_dev->snapshot_list);
    undo_dev->capacity = DEFAULT_CAPACITY / KERNEL_SECTOR_SIZE;
    
    // Allocate storage
    undo_dev->data = vmalloc(DEFAULT_CAPACITY);
    if (!undo_dev->data) {
        pr_err("undo_blk: Failed to allocate device storage\n");
        ret = -ENOMEM;
        goto err_alloc;
    }
    memset(undo_dev->data, 0, DEFAULT_CAPACITY);
    
    // Register block device
    undo_dev->major = register_blkdev(0, DRIVER_NAME);
    if (undo_dev->major < 0) {
        pr_err("undo_blk: Failed to register block device\n");
        ret = undo_dev->major;
        goto err_register;
    }
    
    // Create request queue
    undo_dev->queue = blk_alloc_queue(GFP_KERNEL);
    if (!undo_dev->queue) {
        pr_err("undo_blk: Failed to allocate request queue\n");
        ret = -ENOMEM;
        goto err_queue;
    }
    
    blk_queue_make_request(undo_dev->queue, undo_make_request);
    undo_dev->queue->queuedata = undo_dev;
    
    // Create gendisk
    undo_dev->gd = alloc_disk(1);
    if (!undo_dev->gd) {
        pr_err("undo_blk: Failed to allocate gendisk\n");
        ret = -ENOMEM;
        goto err_disk;
    }
    
    undo_dev->gd->major = undo_dev->major;
    undo_dev->gd->first_minor = 0;
    undo_dev->gd->fops = &undo_fops;
    undo_dev->gd->queue = undo_dev->queue;
    undo_dev->gd->private_data = undo_dev;
    snprintf(undo_dev->gd->disk_name, 32, DEVICE_NAME);
    set_capacity(undo_dev->gd, undo_dev->capacity);
    
    // Create work queue
    undo_dev->work_queue = create_singlethread_workqueue("undo_blk_wq");
    if (!undo_dev->work_queue) {
        pr_err("undo_blk: Failed to create work queue\n");
        ret = -ENOMEM;
        goto err_workqueue;
    }
    
    INIT_WORK(&undo_dev->rollback_work, rollback_work_fn);
    
    // Initialize proc interface
    ret = init_proc_interface(undo_dev);
    if (ret < 0)
        goto err_proc;
    
    // Add disk
    add_disk(undo_dev->gd);
    
    pr_info("undo_blk: Device registered as /dev/%s (major %d)\n", 
            DEVICE_NAME, undo_dev->major);
    pr_info("undo_blk: Capacity: %llu MB\n", 
            (unsigned long long)(undo_dev->capacity * KERNEL_SECTOR_SIZE) >> 20);
    pr_info("undo_blk: Proc interface: /proc/%s/\n", DRIVER_NAME);
    
    return 0;
    
err_proc:
    destroy_workqueue(undo_dev->work_queue);
err_workqueue:
    put_disk(undo_dev->gd);
err_disk:
    blk_cleanup_queue(undo_dev->queue);
err_queue:
    unregister_blkdev(undo_dev->major, DRIVER_NAME);
err_register:
    vfree(undo_dev->data);
err_alloc:
    kfree(undo_dev);
    return ret;
}

// Module cleanup
static void __exit undo_blk_exit(void)
{
    struct journal_entry *entry, *tmp_entry;
    struct snapshot_entry *snap, *tmp_snap;
    
    pr_info("undo_blk: Shutting down Undo Block Device Driver\n");
    
    if (!undo_dev)
        return;
    
    // Cleanup proc interface
    cleanup_proc_interface(undo_dev);
    
    // Remove disk
    if (undo_dev->gd) {
        del_gendisk(undo_dev->gd);
        put_disk(undo_dev->gd);
    }
    
    // Cleanup work queue
    if (undo_dev->work_queue) {
        flush_workqueue(undo_dev->work_queue);
        destroy_workqueue(undo_dev->work_queue);
    }
    
    // Cleanup queue
    if (undo_dev->queue)
        blk_cleanup_queue(undo_dev->queue);
    
    // Unregister block device
    if (undo_dev->major > 0)
        unregister_blkdev(undo_dev->major, DRIVER_NAME);
    
    // Free journal entries
    list_for_each_entry_safe(entry, tmp_entry, &undo_dev->journal_list, list) {
        list_del(&entry->list);
        if (entry->data)
            vfree(entry->data);
        kfree(entry);
    }
    
    // Free snapshots
    list_for_each_entry_safe(snap, tmp_snap, &undo_dev->snapshot_list, list) {
        list_del(&snap->list);
        kfree(snap);
    }
    
    // Free storage
    if (undo_dev->data)
        vfree(undo_dev->data);
    
    kfree(undo_dev);
    undo_dev = NULL;
    
    pr_info("undo_blk: Driver unloaded\n");
}

module_init(undo_blk_init);
module_exit(undo_blk_exit);
