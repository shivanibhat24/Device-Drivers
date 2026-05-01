#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/poll.h>

#define DEVICE_NAME "walkie"
#define BUF_SIZE    4096
#define MAX_OPENS   2       /* Only two processes may open the device */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("walkie");
MODULE_DESCRIPTION("Two-process IPC character device");

/* ---------- shared ring buffer ---------- */
struct walkie_buf {
    char        data[BUF_SIZE];
    size_t      read_pos;
    size_t      write_pos;
    size_t      len;            /* bytes currently stored */
};

static inline size_t buf_write(struct walkie_buf *b, const char *src, size_t n)
{
    size_t written = 0;
    while (written < n && b->len < BUF_SIZE) {
        b->data[b->write_pos] = src[written++];
        b->write_pos = (b->write_pos + 1) % BUF_SIZE;
        b->len++;
    }
    return written;
}

static inline size_t buf_read(struct walkie_buf *b, char *dst, size_t n)
{
    size_t rd = 0;
    while (rd < n && b->len > 0) {
        dst[rd++] = b->data[b->read_pos];
        b->read_pos = (b->read_pos + 1) % BUF_SIZE;
        b->len--;
    }
    return rd;
}

/* ---------- per-channel state ---------- */
/*
 * Two "sides": side 0 and side 1.
 * Writing to side N puts data into pipe[N].
 * Reading from side N drains pipe[1-N]  (you read what the other wrote).
 */
struct walkie_dev {
    struct walkie_buf   pipe[2];    /* pipe[0]: written by side-0, read by side-1 */
    wait_queue_head_t   wq[2];      /* wq[i] woken when pipe[i] gets new data     */
    struct mutex        lock;
    int                 open_count;
    int                 side[2];    /* which file-descriptor owns which side       */
};

static struct walkie_dev *wdev;
static dev_t              devno;
static struct cdev        wcdev;
static struct class      *wclass;

/* ---------- file operations ---------- */

static int walkie_open(struct inode *inode, struct file *filp)
{
    int my_side;

    mutex_lock(&wdev->lock);

    if (wdev->open_count >= MAX_OPENS) {
        mutex_unlock(&wdev->lock);
        pr_warn("walkie: device already opened by two processes\n");
        return -EBUSY;
    }

    my_side = wdev->open_count;   /* 0 for first opener, 1 for second */
    wdev->open_count++;
    mutex_unlock(&wdev->lock);

    filp->private_data = (void *)(long)my_side;
    pr_info("walkie: process %d opened as side %d\n", current->pid, my_side);
    return 0;
}

static int walkie_release(struct inode *inode, struct file *filp)
{
    int my_side = (int)(long)filp->private_data;

    mutex_lock(&wdev->lock);
    wdev->open_count--;
    mutex_unlock(&wdev->lock);

    /* Wake the other side so it can detect EOF */
    wake_up_interruptible(&wdev->wq[my_side]);

    pr_info("walkie: side %d (pid %d) closed\n", my_side, current->pid);
    return 0;
}

static ssize_t walkie_write(struct file *filp,
                             const char __user *ubuf,
                             size_t count, loff_t *ppos)
{
    int my_side = (int)(long)filp->private_data;
    struct walkie_buf *dst = &wdev->pipe[my_side];  /* write into my own pipe */
    char kbuf[BUF_SIZE];
    size_t to_copy, written;

    if (count == 0)
        return 0;

    to_copy = min(count, (size_t)BUF_SIZE);
    if (copy_from_user(kbuf, ubuf, to_copy))
        return -EFAULT;

    mutex_lock(&wdev->lock);

    if (dst->len == BUF_SIZE) {
        /* Buffer full – block until there is space (non-blocking: return EAGAIN) */
        mutex_unlock(&wdev->lock);
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if (wait_event_interruptible(wdev->wq[my_side], dst->len < BUF_SIZE))
            return -ERESTARTSYS;
        mutex_lock(&wdev->lock);
    }

    written = buf_write(dst, kbuf, to_copy);
    mutex_unlock(&wdev->lock);

    /* Wake the reader on the other side */
    wake_up_interruptible(&wdev->wq[my_side]);

    return (ssize_t)written;
}

static ssize_t walkie_read(struct file *filp,
                            char __user *ubuf,
                            size_t count, loff_t *ppos)
{
    int my_side   = (int)(long)filp->private_data;
    int peer_side = 1 - my_side;
    struct walkie_buf *src = &wdev->pipe[peer_side]; /* read from peer's pipe */
    char kbuf[BUF_SIZE];
    size_t rd;

    if (count == 0)
        return 0;

    mutex_lock(&wdev->lock);

    /* Block until data is available or peer has disconnected */
    while (src->len == 0) {
        mutex_unlock(&wdev->lock);

        if (wdev->open_count < MAX_OPENS) /* peer closed */
            return 0;                     /* EOF         */

        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        if (wait_event_interruptible(wdev->wq[peer_side], src->len > 0))
            return -ERESTARTSYS;

        mutex_lock(&wdev->lock);
    }

    rd = buf_read(src, kbuf, min(count, (size_t)BUF_SIZE));
    mutex_unlock(&wdev->lock);

    /* Space freed – wake writer on peer side if it was blocked */
    wake_up_interruptible(&wdev->wq[peer_side]);

    if (copy_to_user(ubuf, kbuf, rd))
        return -EFAULT;

    return (ssize_t)rd;
}

static __poll_t walkie_poll(struct file *filp, poll_table *wait)
{
    int my_side   = (int)(long)filp->private_data;
    int peer_side = 1 - my_side;
    __poll_t mask = 0;

    poll_wait(filp, &wdev->wq[peer_side], wait);
    poll_wait(filp, &wdev->wq[my_side],   wait);

    mutex_lock(&wdev->lock);
    if (wdev->pipe[peer_side].len > 0)         mask |= EPOLLIN  | EPOLLRDNORM;
    if (wdev->pipe[my_side].len   < BUF_SIZE)  mask |= EPOLLOUT | EPOLLWRNORM;
    if (wdev->open_count < MAX_OPENS)          mask |= EPOLLHUP;
    mutex_unlock(&wdev->lock);

    return mask;
}

static const struct file_operations walkie_fops = {
    .owner   = THIS_MODULE,
    .open    = walkie_open,
    .release = walkie_release,
    .read    = walkie_read,
    .write   = walkie_write,
    .poll    = walkie_poll,
};

/* ---------- module init / exit ---------- */

static int __init walkie_init(void)
{
    int ret;

    wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
    if (!wdev)
        return -ENOMEM;

    mutex_init(&wdev->lock);
    init_waitqueue_head(&wdev->wq[0]);
    init_waitqueue_head(&wdev->wq[1]);

    ret = alloc_chrdev_region(&devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("walkie: alloc_chrdev_region failed: %d\n", ret);
        goto err_free;
    }

    cdev_init(&wcdev, &walkie_fops);
    wcdev.owner = THIS_MODULE;
    ret = cdev_add(&wcdev, devno, 1);
    if (ret < 0) {
        pr_err("walkie: cdev_add failed: %d\n", ret);
        goto err_unreg;
    }

    wclass = class_create(DEVICE_NAME);
    if (IS_ERR(wclass)) {
        ret = PTR_ERR(wclass);
        pr_err("walkie: class_create failed: %d\n", ret);
        goto err_cdev;
    }

    if (IS_ERR(device_create(wclass, NULL, devno, NULL, DEVICE_NAME))) {
        ret = -ENOMEM;
        pr_err("walkie: device_create failed\n");
        goto err_class;
    }

    pr_info("walkie: loaded – /dev/walkie is major %d\n", MAJOR(devno));
    return 0;

err_class:  class_destroy(wclass);
err_cdev:   cdev_del(&wcdev);
err_unreg:  unregister_chrdev_region(devno, 1);
err_free:   kfree(wdev);
    return ret;
}

static void __exit walkie_exit(void)
{
    device_destroy(wclass, devno);
    class_destroy(wclass);
    cdev_del(&wcdev);
    unregister_chrdev_region(devno, 1);
    kfree(wdev);
    pr_info("walkie: unloaded\n");
}

module_init(walkie_init);
module_exit(walkie_exit);
