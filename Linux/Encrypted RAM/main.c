/*
 * Encrypted RAM Disk using Linux Crypto API
 * 
 * This program creates an encrypted RAM disk device using the Linux kernel's
 * request queue mechanism and crypto API for encryption.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <crypto/skcipher.h>
#include <linux/slab.h>
#include <linux/random.h>

/* Module parameters */
static int ramdisk_size = 16 * 1024;  /* Size in KB, default 16MB */
module_param(ramdisk_size, int, 0);
MODULE_PARM_DESC(ramdisk_size, "Size of the RAM disk in KB");

static char *crypto_alg = "aes";
module_param(crypto_alg, charp, 0);
MODULE_PARM_DESC(crypto_alg, "Encryption algorithm to use");

static char *crypto_mode = "cbc";
module_param(crypto_mode, charp, 0);
MODULE_PARM_DESC(crypto_mode, "Encryption mode to use");

#define KERNEL_SECTOR_SIZE 512
#define CRYPTO_KEY_SIZE 32  /* 256-bit key */
#define CRYPTO_IV_SIZE 16   /* 128-bit IV */

/* Device data structure */
struct encrypted_ramdisk_dev {
    /* RAM disk data */
    unsigned long size;      /* Size in sectors */
    u8 *data;                /* Data buffer */
    
    /* Crypto information */
    u8 key[CRYPTO_KEY_SIZE];
    struct crypto_skcipher *tfm;
    char *cipher_name;
    
    /* Block device structures */
    spinlock_t lock;
    struct request_queue *queue;
    struct gendisk *gd;
};

static struct encrypted_ramdisk_dev *ramdisk_dev = NULL;

/* Crypto helper functions */
static int init_crypto(struct encrypted_ramdisk_dev *dev)
{
    char cipher_name[128];
    int ret;

    /* Create the cipher name (e.g., "aes-cbc") */
    snprintf(cipher_name, sizeof(cipher_name), "%s-%s", crypto_alg, crypto_mode);
    dev->cipher_name = kstrdup(cipher_name, GFP_KERNEL);
    if (!dev->cipher_name)
        return -ENOMEM;

    /* Allocate transform */
    dev->tfm = crypto_alloc_skcipher(dev->cipher_name, 0, 0);
    if (IS_ERR(dev->tfm)) {
        ret = PTR_ERR(dev->tfm);
        pr_err("Error allocating crypto transform: %d\n", ret);
        kfree(dev->cipher_name);
        return ret;
    }

    /* Generate a random key */
    get_random_bytes(dev->key, CRYPTO_KEY_SIZE);

    /* Set the key */
    ret = crypto_skcipher_setkey(dev->tfm, dev->key, CRYPTO_KEY_SIZE);
    if (ret) {
        pr_err("Error setting key: %d\n", ret);
        crypto_free_skcipher(dev->tfm);
        kfree(dev->cipher_name);
        return ret;
    }

    return 0;
}

static void cleanup_crypto(struct encrypted_ramdisk_dev *dev)
{
    crypto_free_skcipher(dev->tfm);
    kfree(dev->cipher_name);
}

/* 
 * Encrypt or decrypt a sector
 * dir: 1 = encrypt, 0 = decrypt
 */
static int crypt_sector(struct encrypted_ramdisk_dev *dev, 
                        sector_t sector, int dir)
{
    struct skcipher_request *req;
    struct scatterlist sg;
    DECLARE_CRYPTO_WAIT(wait);
    u8 iv[CRYPTO_IV_SIZE];
    u8 *buffer;
    u8 *sector_data;
    int ret;

    req = skcipher_request_alloc(dev->tfm, GFP_NOIO);
    if (!req)
        return -ENOMEM;

    /* Create an IV based on the sector number */
    memset(iv, 0, CRYPTO_IV_SIZE);
    *(sector_t *)iv = sector;

    /* Allocate a temporary buffer for in-place encryption */
    buffer = kmalloc(KERNEL_SECTOR_SIZE, GFP_NOIO);
    if (!buffer) {
        skcipher_request_free(req);
        return -ENOMEM;
    }

    /* Get the pointer to the sector data */
    sector_data = dev->data + (sector * KERNEL_SECTOR_SIZE);
    
    if (dir) {
        /* For encryption: copy data to temp buffer first */
        memcpy(buffer, sector_data, KERNEL_SECTOR_SIZE);
    } else {
        /* For decryption: use the encrypted data directly */
        memcpy(buffer, sector_data, KERNEL_SECTOR_SIZE);
    }

    sg_init_one(&sg, buffer, KERNEL_SECTOR_SIZE);
    skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
                                  CRYPTO_TFM_REQ_MAY_SLEEP,
                                  crypto_req_done, &wait);
    skcipher_request_set_crypt(req, &sg, &sg, KERNEL_SECTOR_SIZE, iv);

    if (dir)
        ret = crypto_wait_req(crypto_skcipher_encrypt(req), &wait);
    else
        ret = crypto_wait_req(crypto_skcipher_decrypt(req), &wait);

    if (ret == 0) {
        /* Copy the result back to the device data */
        memcpy(sector_data, buffer, KERNEL_SECTOR_SIZE);
    }

    kfree(buffer);
    skcipher_request_free(req);
    return ret;
}

/* Request handler function for the queue */
static void encrypted_ramdisk_request(struct request_queue *q)
{
    struct request *req;
    struct encrypted_ramdisk_dev *dev = q->queuedata;
    
    req = blk_fetch_request(q);
    while (req != NULL) {
        if (req->cmd_type != REQ_TYPE_FS) {
            pr_notice("Skip non-fs request\n");
            __blk_end_request_all(req, -EIO);
            continue;
        }

        encrypted_ramdisk_transfer(dev, blk_rq_pos(req), blk_rq_cur_sectors(req),
                                bio_data(req->bio), rq_data_dir(req));
        if (!__blk_end_request_cur(req, 0))
            req = blk_fetch_request(q);
    }
}

/* Transfer function to handle read/write with encryption/decryption */
static void encrypted_ramdisk_transfer(struct encrypted_ramdisk_dev *dev, 
                                    sector_t sector, unsigned long nsect,
                                    char *buffer, int write)
{
    unsigned long offset = sector * KERNEL_SECTOR_SIZE;
    unsigned long nbytes = nsect * KERNEL_SECTOR_SIZE;
    sector_t current_sector;
    int i, ret;

    if ((offset + nbytes) > dev->size * KERNEL_SECTOR_SIZE) {
        pr_notice("Beyond-end write (%ld %ld)\n", offset, nbytes);
        return;
    }

    /* Process each sector individually for encryption/decryption */
    for (i = 0; i < nsect; i++) {
        current_sector = sector + i;
        
        if (write) {
            /* Write operation: copy data from buffer to RAM disk then encrypt */
            memcpy(dev->data + (current_sector * KERNEL_SECTOR_SIZE),
                   buffer + (i * KERNEL_SECTOR_SIZE),
                   KERNEL_SECTOR_SIZE);
            
            ret = crypt_sector(dev, current_sector, 1); /* encrypt */
            if (ret) {
                pr_err("Encryption error in sector %lld: %d\n", 
                       (unsigned long long)current_sector, ret);
            }
        } else {
            /* Read operation: decrypt data then copy to buffer */
            ret = crypt_sector(dev, current_sector, 0); /* decrypt */
            if (ret) {
                pr_err("Decryption error in sector %lld: %d\n", 
                       (unsigned long long)current_sector, ret);
            }
            
            memcpy(buffer + (i * KERNEL_SECTOR_SIZE),
                   dev->data + (current_sector * KERNEL_SECTOR_SIZE),
                   KERNEL_SECTOR_SIZE);
        }
    }
}

/* Block device operations */
static int encrypted_ramdisk_open(struct block_device *bdev, fmode_t mode)
{
    return 0;
}

static void encrypted_ramdisk_release(struct gendisk *gd, fmode_t mode)
{
    return;
}

static int encrypted_ramdisk_ioctl(struct block_device *bdev, fmode_t mode,
                                 unsigned int cmd, unsigned long arg)
{
    struct hd_geometry geo;

    switch (cmd) {
    /* The only ioctl implemented is HDIO_GETGEO */
    case HDIO_GETGEO:
        geo.cylinders = ramdisk_dev->size / (16 * 63);
        geo.heads = 16;
        geo.sectors = 63;
        geo.start = 0;
        if (copy_to_user((void __user *)arg, &geo, sizeof(geo)))
            return -EFAULT;
        return 0;
    }

    return -ENOTTY;
}

static struct block_device_operations encrypted_ramdisk_ops = {
    .owner          = THIS_MODULE,
    .open           = encrypted_ramdisk_open,
    .release        = encrypted_ramdisk_release,
    .ioctl          = encrypted_ramdisk_ioctl,
};

/* Module initialization and cleanup */
static int __init encrypted_ramdisk_init(void)
{
    int ret;

    /* Allocate the device structure */
    ramdisk_dev = kzalloc(sizeof(struct encrypted_ramdisk_dev), GFP_KERNEL);
    if (!ramdisk_dev)
        return -ENOMEM;

    /* Initialize the device structure */
    ramdisk_dev->size = ramdisk_size * 2; /* Size in sectors (512 bytes per sector) */
    spin_lock_init(&ramdisk_dev->lock);

    /* Allocate memory for the device */
    ramdisk_dev->data = vzalloc(ramdisk_dev->size * KERNEL_SECTOR_SIZE);
    if (!ramdisk_dev->data) {
        ret = -ENOMEM;
        goto out_free_dev;
    }

    /* Initialize crypto */
    ret = init_crypto(ramdisk_dev);
    if (ret)
        goto out_free_data;

    /* Initialize the request queue */
    ramdisk_dev->queue = blk_init_queue(encrypted_ramdisk_request, &ramdisk_dev->lock);
    if (!ramdisk_dev->queue) {
        ret = -ENOMEM;
        goto out_cleanup_crypto;
    }
    blk_queue_logical_block_size(ramdisk_dev->queue, KERNEL_SECTOR_SIZE);
    ramdisk_dev->queue->queuedata = ramdisk_dev;

    /* Register the device */
    major_num = register_blkdev(0, "eramdisk");
    if (major_num < 0) {
        ret = major_num;
        goto out_cleanup_queue;
    }

    /* Create the gendisk structure */
    ramdisk_dev->gd = alloc_disk(16);
    if (!ramdisk_dev->gd) {
        ret = -ENOMEM;
        goto out_unregister_blkdev;
    }

    /* Initialize the gendisk */
    ramdisk_dev->gd->major = major_num;
    ramdisk_dev->gd->first_minor = 0;
    ramdisk_dev->gd->fops = &encrypted_ramdisk_ops;
    ramdisk_dev->gd->queue = ramdisk_dev->queue;
    ramdisk_dev->gd->private_data = ramdisk_dev;
    snprintf(ramdisk_dev->gd->disk_name, 32, "eramdisk");
    set_capacity(ramdisk_dev->gd, ramdisk_dev->size);

    /* Make the device available */
    add_disk(ramdisk_dev->gd);

    pr_info("Encrypted RAM disk created with %s encryption (%d KB)\n", 
            ramdisk_dev->cipher_name, ramdisk_size);
    return 0;

out_unregister_blkdev:
    unregister_blkdev(major_num, "eramdisk");
out_cleanup_queue:
    blk_cleanup_queue(ramdisk_dev->queue);
out_cleanup_crypto:
    cleanup_crypto(ramdisk_dev);
out_free_data:
    vfree(ramdisk_dev->data);
out_free_dev:
    kfree(ramdisk_dev);
    return ret;
}

static void __exit encrypted_ramdisk_exit(void)
{
    if (ramdisk_dev) {
        /* Delete the gendisk */
        if (ramdisk_dev->gd) {
            del_gendisk(ramdisk_dev->gd);
            put_disk(ramdisk_dev->gd);
        }

        /* Cleanup the queue */
        if (ramdisk_dev->queue)
            blk_cleanup_queue(ramdisk_dev->queue);

        /* Unregister the block device */
        unregister_blkdev(major_num, "eramdisk");

        /* Clean up crypto resources */
        cleanup_crypto(ramdisk_dev);

        /* Free the data buffer */
        vfree(ramdisk_dev->data);

        /* Free the device structure */
        kfree(ramdisk_dev);
    }

    pr_info("Encrypted RAM disk removed\n");
}

module_init(encrypted_ramdisk_init);
module_exit(encrypted_ramdisk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("Encrypted RAM Disk using Linux Crypto API");
