// SPDX-License-Identifier: GPL-2.0
//
// edu_driver.c — Linux PCI kernel driver for the QEMU EDU device
//
// The EDU device (hw/misc/edu.c in QEMU source) is a virtual PCI device
// designed specifically for driver development education. It exposes:
//   - A live clock register (free-running counter)
//   - A factorial compute engine (write N, read N!)
//   - An interrupt mechanism (raised when factorial is done)
//   - A DMA engine (not used in this driver, left as an exercise)
//
// This driver binds to the EDU device via PCI vendor/device ID,
// maps its MMIO BAR, registers an IRQ handler, and exposes the
// device to userspace as /dev/edu0.
//
// Author: Shivani
// Board:  QEMU virt (ARM64, cortex-a57)

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/io.h>

// ---------------------------------------------------------------------------
// PCI identity
// ---------------------------------------------------------------------------
#define EDU_VENDOR_ID   0x1234
#define EDU_DEVICE_ID   0x11e8

// ---------------------------------------------------------------------------
// MMIO register map (offsets from BAR 0)
// ---------------------------------------------------------------------------
#define EDU_REG_ID          0x00  // Device identification (read-only)
#define EDU_REG_LIVECLOCK   0x04  // Free-running counter (read-only)
#define EDU_REG_FACTORIAL   0x08  // Write N to compute N!, read result
#define EDU_REG_STATUS      0x20  // Status/control flags
#define EDU_REG_IRQ_STATUS  0x24  // Interrupt status (read to check)
#define EDU_REG_IRQ_RAISE   0x60  // Write any value to raise an interrupt
#define EDU_REG_IRQ_ACK     0x64  // Write IRQ status value to acknowledge

// Status register bits
#define EDU_STATUS_BUSY     (1 << 0)  // Set while computing factorial
#define EDU_STATUS_IRQ_EN   (1 << 7)  // Set to enable interrupts

// Character device
#define EDU_MINOR 0
#define EDU_COUNT 1

// ---------------------------------------------------------------------------
// Per-device state
// ---------------------------------------------------------------------------
struct edu_device {
    struct pci_dev  *pdev;     // PCI device handle
    void __iomem    *mmio;     // Mapped BAR 0
    int              irq;      // Assigned IRQ number
    struct cdev      cdev;     // Character device
    dev_t            devnum;   // Major/minor numbers
    struct class    *class;    // Device class for /dev node
};

static struct edu_device *edu_dev;

// ---------------------------------------------------------------------------
// IRQ handler — called when the EDU device raises an interrupt
// ---------------------------------------------------------------------------
static irqreturn_t edu_irq_handler(int irq, void *data)
{
    struct edu_device *dev = data;
    u32 status;

    status = ioread32(dev->mmio + EDU_REG_IRQ_STATUS);
    if (!status)
        return IRQ_NONE;  // Not our interrupt

    dev_info(&dev->pdev->dev, "IRQ received! status=0x%x\n", status);

    // Acknowledge by writing the status back
    iowrite32(status, dev->mmio + EDU_REG_IRQ_ACK);
    return IRQ_HANDLED;
}

// ---------------------------------------------------------------------------
// Character device read — returns the live clock register value
// ---------------------------------------------------------------------------
static ssize_t edu_read(struct file *f, char __user *buf,
                         size_t count, loff_t *off)
{
    u32 val;
    char tmp[32];
    int len;

    if (*off != 0)
        return 0;  // EOF on second read

    val = ioread32(edu_dev->mmio + EDU_REG_LIVECLOCK);
    len = snprintf(tmp, sizeof(tmp), "liveclock=0x%08x\n", val);

    if (copy_to_user(buf, tmp, len))
        return -EFAULT;

    *off += len;
    return len;
}

// ---------------------------------------------------------------------------
// Character device write — triggers a factorial computation
//   echo "5" > /dev/edu0   →  computes 5! = 120, result in dmesg
// ---------------------------------------------------------------------------
static ssize_t edu_write(struct file *f, const char __user *buf,
                          size_t count, loff_t *off)
{
    char tmp[16];
    u32 n;

    if (count >= sizeof(tmp))
        return -EINVAL;
    if (copy_from_user(tmp, buf, count))
        return -EFAULT;

    tmp[count] = '\0';
    if (kstrtou32(tmp, 10, &n))
        return -EINVAL;

    dev_info(&edu_dev->pdev->dev, "Computing %u!\n", n);

    // Write N to the factorial register — device starts computing
    iowrite32(n, edu_dev->mmio + EDU_REG_FACTORIAL);

    // Poll until device clears the BUSY flag
    while (ioread32(edu_dev->mmio + EDU_REG_STATUS) & EDU_STATUS_BUSY)
        cpu_relax();

    // Read back the result
    n = ioread32(edu_dev->mmio + EDU_REG_FACTORIAL);
    dev_info(&edu_dev->pdev->dev, "Result: %u\n", n);

    return count;
}

static const struct file_operations edu_fops = {
    .owner  = THIS_MODULE,
    .read   = edu_read,
    .write  = edu_write,
};

// ---------------------------------------------------------------------------
// PCI probe — called by the kernel when EDU device is detected on the bus
// ---------------------------------------------------------------------------
static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    int ret;
    u32 edu_id;

    edu_dev = devm_kzalloc(&pdev->dev, sizeof(*edu_dev), GFP_KERNEL);
    if (!edu_dev)
        return -ENOMEM;

    edu_dev->pdev = pdev;

    // Step 1: Enable the PCI device
    ret = pci_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "pci_enable_device failed: %d\n", ret);
        return ret;
    }

    // Step 2: Reserve BAR 0 (MMIO region)
    ret = pci_request_region(pdev, 0, "edu");
    if (ret) {
        dev_err(&pdev->dev, "pci_request_region failed: %d\n", ret);
        goto err_disable;
    }

    // Step 3: Map BAR 0 into kernel virtual address space
    edu_dev->mmio = pci_iomap(pdev, 0, 0);
    if (!edu_dev->mmio) {
        ret = -ENOMEM;
        goto err_release;
    }

    // Step 4: Read and log the device ID register
    edu_id = ioread32(edu_dev->mmio + EDU_REG_ID);
    dev_info(&pdev->dev, "EDU device found! ID=0x%08x\n", edu_id);

    // Step 5: Enable interrupts in the device status register
    iowrite32(EDU_STATUS_IRQ_EN, edu_dev->mmio + EDU_REG_STATUS);

    // Step 6: Register the IRQ handler
    ret = request_irq(pdev->irq, edu_irq_handler,
                      IRQF_SHARED, "edu", edu_dev);
    if (ret) {
        dev_err(&pdev->dev, "request_irq failed: %d\n", ret);
        goto err_unmap;
    }
    edu_dev->irq = pdev->irq;

    // Step 7: Allocate a character device number
    ret = alloc_chrdev_region(&edu_dev->devnum, EDU_MINOR, EDU_COUNT, "edu");
    if (ret)
        goto err_free_irq;

    // Step 8: Register the character device
    cdev_init(&edu_dev->cdev, &edu_fops);
    ret = cdev_add(&edu_dev->cdev, edu_dev->devnum, EDU_COUNT);
    if (ret)
        goto err_unreg_region;

    // Step 9: Create /dev/edu0 via sysfs
    edu_dev->class = class_create("edu");
    if (IS_ERR(edu_dev->class)) {
        ret = PTR_ERR(edu_dev->class);
        goto err_cdev_del;
    }

    device_create(edu_dev->class, NULL, edu_dev->devnum, NULL, "edu0");
    pci_set_drvdata(pdev, edu_dev);

    dev_info(&pdev->dev, "edu0 ready at /dev/edu0\n");
    return 0;

    // Error unwinding (reverse order of setup)
err_cdev_del:
    cdev_del(&edu_dev->cdev);
err_unreg_region:
    unregister_chrdev_region(edu_dev->devnum, EDU_COUNT);
err_free_irq:
    free_irq(edu_dev->irq, edu_dev);
err_unmap:
    pci_iounmap(pdev, edu_dev->mmio);
err_release:
    pci_release_region(pdev, 0);
err_disable:
    pci_disable_device(pdev);
    return ret;
}

// ---------------------------------------------------------------------------
// PCI remove — called when driver is unloaded or device is removed
// ---------------------------------------------------------------------------
static void edu_remove(struct pci_dev *pdev)
{
    struct edu_device *dev = pci_get_drvdata(pdev);

    device_destroy(dev->class, dev->devnum);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devnum, EDU_COUNT);
    free_irq(dev->irq, dev);
    pci_iounmap(pdev, dev->mmio);
    pci_release_region(pdev, 0);
    pci_disable_device(pdev);

    dev_info(&pdev->dev, "EDU device removed\n");
}

// ---------------------------------------------------------------------------
// PCI device ID table — tells the kernel which device this driver handles
// ---------------------------------------------------------------------------
static const struct pci_device_id edu_ids[] = {
    { PCI_DEVICE(EDU_VENDOR_ID, EDU_DEVICE_ID) },
    { 0 }
};
MODULE_DEVICE_TABLE(pci, edu_ids);

static struct pci_driver edu_driver = {
    .name     = "edu",
    .id_table = edu_ids,
    .probe    = edu_probe,
    .remove   = edu_remove,
};

module_pci_driver(edu_driver);

MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("Linux PCI kernel driver for the QEMU EDU virtual device");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
