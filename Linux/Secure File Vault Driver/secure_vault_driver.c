/**
 * secure_vault_driver.c - A secure file vault driver that restricts access
 * to files based on process ID, group ID, or session ID.
 * 
 * This driver implements a secure vault that only allows access to files
 * if the accessing process belongs to the authorized PID, group, or session.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/dcache.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("A secure file vault driver with process/group/session access restrictions");
MODULE_VERSION("1.0");

#define SECURE_VAULT_NAME "secure_vault"
#define MAX_FILES 256
#define MAX_FILENAME_LEN 255
#define MAX_DATA_SIZE (4 * 1024 * 1024)  // 4MB max file size

/* Access control types */
enum access_type {
    ACCESS_PID = 0,
    ACCESS_GID = 1,
    ACCESS_SESSION = 2
};

/* Access control structure */
struct access_control {
    enum access_type type;
    union {
        pid_t pid;
        gid_t gid;
        pid_t session;
    };
};

/* Secure file structure */
struct secure_file {
    char filename[MAX_FILENAME_LEN + 1];
    void *data;
    size_t size;
    struct access_control access;
    struct list_head list;
};

/* Vault control structure */
struct vault_control {
    struct list_head files;
    struct mutex lock;
    struct dentry *debugfs_dir;
    int file_count;
};

static struct vault_control *vault = NULL;

/* Check if current process has access to the file */
static bool has_access(struct secure_file *file)
{
    struct task_struct *current_task = current;
    const struct cred *cred = current_cred();

    switch (file->access.type) {
        case ACCESS_PID:
            return current_task->pid == file->access.pid;
        
        case ACCESS_GID:
            return in_egroup_p(file->access.gid);
        
        case ACCESS_SESSION:
            return current_task->signal->session == file->access.session;
        
        default:
            return false;
    }
}

/* Find a file by name */
static struct secure_file *find_file(const char *filename)
{
    struct secure_file *file;
    
    list_for_each_entry(file, &vault->files, list) {
        if (strcmp(file->filename, filename) == 0) {
            return file;
        }
    }
    
    return NULL;
}

/* File operations */
static ssize_t vault_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct secure_file *file;
    ssize_t ret;

    mutex_lock(&vault->lock);
    
    file = find_file(filp->f_path.dentry->d_name.name);
    if (!file) {
        mutex_unlock(&vault->lock);
        return -ENOENT;
    }
    
    if (!has_access(file)) {
        mutex_unlock(&vault->lock);
        return -EACCES;
    }
    
    if (*f_pos >= file->size) {
        mutex_unlock(&vault->lock);
        return 0;  // EOF
    }
    
    count = min(count, file->size - *f_pos);
    ret = copy_to_user(buf, file->data + *f_pos, count);
    if (ret) {
        mutex_unlock(&vault->lock);
        return -EFAULT;
    }
    
    *f_pos += count;
    
    mutex_unlock(&vault->lock);
    return count;
}

static ssize_t vault_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct secure_file *file;
    void *new_data;
    ssize_t ret;

    if (count > MAX_DATA_SIZE) {
        return -EFBIG;
    }

    mutex_lock(&vault->lock);
    
    file = find_file(filp->f_path.dentry->d_name.name);
    if (!file) {
        mutex_unlock(&vault->lock);
        return -ENOENT;
    }
    
    if (!has_access(file)) {
        mutex_unlock(&vault->lock);
        return -EACCES;
    }
    
    if (*f_pos + count > file->size) {
        /* Need to expand the buffer */
        new_data = krealloc(file->data, *f_pos + count, GFP_KERNEL);
        if (!new_data) {
            mutex_unlock(&vault->lock);
            return -ENOMEM;
        }
        
        file->data = new_data;
        file->size = *f_pos + count;
    }
    
    ret = copy_from_user(file->data + *f_pos, buf, count);
    if (ret) {
        mutex_unlock(&vault->lock);
        return -EFAULT;
    }
    
    *f_pos += count;
    
    mutex_unlock(&vault->lock);
    return count;
}

static int vault_open(struct inode *inode, struct file *filp)
{
    /* Check will be done at read/write time */
    return 0;
}

static int vault_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/* File operations structure */
static const struct file_operations vault_fops = {
    .owner = THIS_MODULE,
    .read = vault_read,
    .write = vault_write,
    .open = vault_open,
    .release = vault_release,
};

/* DebugFS interface for vault management */

/* Create a new secure file */
static ssize_t create_file_write(struct file *filp, const char __user *ubuf, size_t len, loff_t *ppos)
{
    char buf[MAX_FILENAME_LEN + 32];  /* filename + access type + access id */
    struct secure_file *new_file;
    unsigned long access_id;
    char *filename, *access_type_str, *access_id_str;
    enum access_type access_type;
    
    if (len > sizeof(buf) - 1)
        return -EINVAL;
    
    if (copy_from_user(buf, ubuf, len))
        return -EFAULT;
    
    buf[len] = '\0';
    
    /* Parse input: format is "filename access_type access_id" */
    filename = buf;
    access_type_str = strchr(filename, ' ');
    if (!access_type_str)
        return -EINVAL;
    
    *access_type_str++ = '\0';
    access_id_str = strchr(access_type_str, ' ');
    if (!access_id_str)
        return -EINVAL;
    
    *access_id_str++ = '\0';
    
    /* Get access type */
    if (strcmp(access_type_str, "pid") == 0)
        access_type = ACCESS_PID;
    else if (strcmp(access_type_str, "gid") == 0)
        access_type = ACCESS_GID;
    else if (strcmp(access_type_str, "session") == 0)
        access_type = ACCESS_SESSION;
    else
        return -EINVAL;
    
    /* Get access ID */
    if (kstrtoul(access_id_str, 10, &access_id))
        return -EINVAL;
    
    mutex_lock(&vault->lock);
    
    /* Check if file already exists */
    if (find_file(filename)) {
        mutex_unlock(&vault->lock);
        return -EEXIST;
    }
    
    /* Check if we have reached the maximum number of files */
    if (vault->file_count >= MAX_FILES) {
        mutex_unlock(&vault->lock);
        return -ENOMEM;
    }
    
    /* Create a new file */
    new_file = kzalloc(sizeof(struct secure_file), GFP_KERNEL);
    if (!new_file) {
        mutex_unlock(&vault->lock);
        return -ENOMEM;
    }
    
    strncpy(new_file->filename, filename, MAX_FILENAME_LEN);
    new_file->filename[MAX_FILENAME_LEN] = '\0';
    new_file->size = 0;
    new_file->data = NULL;
    new_file->access.type = access_type;
    
    switch (access_type) {
        case ACCESS_PID:
            new_file->access.pid = (pid_t)access_id;
            break;
        case ACCESS_GID:
            new_file->access.gid = (gid_t)access_id;
            break;
        case ACCESS_SESSION:
            new_file->access.session = (pid_t)access_id;
            break;
    }
    
    list_add(&new_file->list, &vault->files);
    vault->file_count++;
    
    /* Create a file in debugfs */
    debugfs_create_file(new_file->filename, 0600, vault->debugfs_dir, NULL, &vault_fops);
    
    mutex_unlock(&vault->lock);
    
    *ppos += len;
    return len;
}

/* Delete a secure file */
static ssize_t delete_file_write(struct file *filp, const char __user *ubuf, size_t len, loff_t *ppos)
{
    char buf[MAX_FILENAME_LEN + 1];
    struct secure_file *file, *tmp;
    struct dentry *dentry;
    
    if (len > sizeof(buf) - 1)
        return -EINVAL;
    
    if (copy_from_user(buf, ubuf, len))
        return -EFAULT;
    
    buf[len] = '\0';
    if (buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    
    mutex_lock(&vault->lock);
    
    /* Find and remove the file */
    list_for_each_entry_safe(file, tmp, &vault->files, list) {
        if (strcmp(file->filename, buf) == 0) {
            list_del(&file->list);
            vault->file_count--;
            
            /* Remove the file from debugfs */
            dentry = debugfs_lookup(file->filename, vault->debugfs_dir);
            if (dentry) {
                debugfs_remove(dentry);
                dput(dentry);
            }
            
            /* Free the file data */
            if (file->data)
                kfree(file->data);
            kfree(file);
            
            mutex_unlock(&vault->lock);
            *ppos += len;
            return len;
        }
    }
    
    mutex_unlock(&vault->lock);
    return -ENOENT;
}

/* List all secure files */
static ssize_t list_files_read(struct file *filp, char __user *ubuf, size_t len, loff_t *ppos)
{
    char *buf;
    struct secure_file *file;
    size_t buf_size = 0;
    size_t total_size = 0;
    size_t offset = 0;
    int ret;
    
    /* Calculate the size needed for the buffer */
    mutex_lock(&vault->lock);
    list_for_each_entry(file, &vault->files, list) {
        /* "filename [pid|gid|session] id size\n" */
        total_size += strlen(file->filename) + 20;
    }
    
    /* Add 1 for the null terminator */
    total_size += 1;
    
    if (*ppos >= total_size) {
        mutex_unlock(&vault->lock);
        return 0;
    }
    
    buf = kzalloc(total_size, GFP_KERNEL);
    if (!buf) {
        mutex_unlock(&vault->lock);
        return -ENOMEM;
    }
    
    /* Fill the buffer */
    list_for_each_entry(file, &vault->files, list) {
        const char *type_str;
        
        switch (file->access.type) {
            case ACCESS_PID:
                type_str = "pid";
                buf_size = snprintf(buf + offset, total_size - offset,
                                   "%s %s %d %zu\n",
                                   file->filename, type_str, file->access.pid, file->size);
                break;
            case ACCESS_GID:
                type_str = "gid";
                buf_size = snprintf(buf + offset, total_size - offset,
                                   "%s %s %d %zu\n",
                                   file->filename, type_str, file->access.gid, file->size);
                break;
            case ACCESS_SESSION:
                type_str = "session";
                buf_size = snprintf(buf + offset, total_size - offset,
                                   "%s %s %d %zu\n",
                                   file->filename, type_str, file->access.session, file->size);
                break;
        }
        
        offset += buf_size;
    }
    
    mutex_unlock(&vault->lock);
    
    /* Copy data to user space */
    if (*ppos + len > total_size)
        len = total_size - *ppos;
    
    ret = copy_to_user(ubuf, buf + *ppos, len);
    kfree(buf);
    
    if (ret)
        return -EFAULT;
    
    *ppos += len;
    return len;
}

/* Get current process information */
static ssize_t process_info_read(struct file *filp, char __user *ubuf, size_t len, loff_t *ppos)
{
    char buf[128];
    struct task_struct *task = current;
    const struct cred *cred = current_cred();
    size_t buf_size;
    
    if (*ppos > 0)
        return 0;
    
    buf_size = snprintf(buf, sizeof(buf),
                       "PID: %d\nSession: %d\nPGRP: %d\nUID: %d\nGID: %d\n",
                       task->pid, task->signal->session,
                       task_pgrp_nr(task), from_kuid(&init_user_ns, cred->uid),
                       from_kgid(&init_user_ns, cred->gid));
    
    if (len < buf_size)
        return -EINVAL;
    
    if (copy_to_user(ubuf, buf, buf_size))
        return -EFAULT;
    
    *ppos += buf_size;
    return buf_size;
}

/* DebugFS file operations */
static const struct file_operations create_file_fops = {
    .owner = THIS_MODULE,
    .write = create_file_write,
};

static const struct file_operations delete_file_fops = {
    .owner = THIS_MODULE,
    .write = delete_file_write,
};

static const struct file_operations list_files_fops = {
    .owner = THIS_MODULE,
    .read = list_files_read,
};

static const struct file_operations process_info_fops = {
    .owner = THIS_MODULE,
    .read = process_info_read,
};

/* Initialize the module */
static int __init secure_vault_init(void)
{
    pr_info("Secure Vault: initializing\n");
    
    /* Allocate vault control structure */
    vault = kzalloc(sizeof(struct vault_control), GFP_KERNEL);
    if (!vault) {
        pr_err("Secure Vault: failed to allocate memory\n");
        return -ENOMEM;
    }
    
    /* Initialize vault */
    INIT_LIST_HEAD(&vault->files);
    mutex_init(&vault->lock);
    vault->file_count = 0;
    
    /* Create debugfs directory */
    vault->debugfs_dir = debugfs_create_dir(SECURE_VAULT_NAME, NULL);
    if (!vault->debugfs_dir) {
        pr_err("Secure Vault: failed to create debugfs directory\n");
        kfree(vault);
        return -ENODEV;
    }
    
    /* Create control files */
    debugfs_create_file("create", 0600, vault->debugfs_dir, NULL, &create_file_fops);
    debugfs_create_file("delete", 0600, vault->debugfs_dir, NULL, &delete_file_fops);
    debugfs_create_file("list", 0400, vault->debugfs_dir, NULL, &list_files_fops);
    debugfs_create_file("process_info", 0400, vault->debugfs_dir, NULL, &process_info_fops);
    
    pr_info("Secure Vault: initialized successfully\n");
    return 0;
}

/* Clean up the module */
static void __exit secure_vault_exit(void)
{
    struct secure_file *file, *tmp;
    
    pr_info("Secure Vault: cleaning up\n");
    
    if (!vault)
        return;
    
    /* Remove debugfs entries */
    debugfs_remove_recursive(vault->debugfs_dir);
    
    /* Free all files */
    mutex_lock(&vault->lock);
    list_for_each_entry_safe(file, tmp, &vault->files, list) {
        list_del(&file->list);
        if (file->data)
            kfree(file->data);
        kfree(file);
    }
    mutex_unlock(&vault->lock);
    
    /* Free vault control structure */
    kfree(vault);
    vault = NULL;
    
    pr_info("Secure Vault: cleanup complete\n");
}

module_init(secure_vault_init);
module_exit(secure_vault_exit);
