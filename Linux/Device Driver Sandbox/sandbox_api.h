/*
 * sandbox_api.h - Linux Kernel Driver Sandbox API
 * 
 * Public API for interacting with the sandbox system
 */

#ifndef _SANDBOX_API_H
#define _SANDBOX_API_H

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/interrupt.h>

/* Sandbox API version */
#define SANDBOX_API_VERSION "1.0.0"

/* Sandbox IOCTL commands */
#define SANDBOX_IOCTL_MAGIC 'S'
#define SANDBOX_IOCTL_LOAD_DRIVER    _IOW(SANDBOX_IOCTL_MAGIC, 1, char*)
#define SANDBOX_IOCTL_UNLOAD_DRIVER  _IOW(SANDBOX_IOCTL_MAGIC, 2, char*)
#define SANDBOX_IOCTL_TRACE_START    _IOW(SANDBOX_IOCTL_MAGIC, 3, char*)
#define SANDBOX_IOCTL_TRACE_STOP     _IOW(SANDBOX_IOCTL_MAGIC, 4, char*)
#define SANDBOX_IOCTL_FUZZ_START     _IOW(SANDBOX_IOCTL_MAGIC, 5, struct sandbox_fuzz_config*)
#define SANDBOX_IOCTL_FUZZ_STOP      _IO(SANDBOX_IOCTL_MAGIC, 6)

/* Sandbox device types */
enum sandbox_device_type {
    SANDBOX_DEV_CHAR,
    SANDBOX_DEV_BLOCK,
    SANDBOX_DEV_NET,
    SANDBOX_DEV_INPUT,
    SANDBOX_DEV_MISC
};

/* Sandbox fuzz configuration */
struct sandbox_fuzz_config {
    char driver_name[64];
    unsigned int modes;
    unsigned int duration_ms;
    unsigned int intensity;
};

/* Sandbox fuzz modes */
#define SANDBOX_FUZZ_IO     (1 << 0)
#define SANDBOX_FUZZ_IRQ    (1 << 1)
#define SANDBOX_FUZZ_MMAP   (1 << 2)
#define SANDBOX_FUZZ_IOCTL  (1 << 3)

/* Sandbox device simulation structure */
struct sandbox_device_sim {
    enum sandbox_device_type type;
    char name[32];
    void *private_data;
    
    /* Simulation callbacks */
    int (*sim_read)(struct sandbox_device_sim *sim, char *buf, size_t len);
    int (*sim_write)(struct sandbox_device_sim *sim, const char *buf, size_t len);
    int (*sim_ioctl)(struct sandbox_device_sim *sim, unsigned int cmd, unsigned long arg);
    int (*sim_mmap)(struct sandbox_device_sim *sim, struct vm_area_struct *vma);
    
    /* Device-specific operations */
    union {
        struct {
            char *buffer;
            size_t buffer_size;
        } char_dev;
        
        struct {
            void *sectors;
            sector_t sector_count;
            unsigned int sector_size;
        } block_dev;
        
        struct {
            struct sk_buff_head rx_queue;
            struct sk_buff_head tx_queue;
        } net_dev;
        
        struct {
            struct input_dev *input_dev;
            unsigned int event_mask;
        } input_dev;
    };
};

/* Sandbox wrapper functions */
struct sandbox_file_operations {
    struct file_operations orig_fops;
    const char *driver_name;
    struct module *owner_module;
};

/* Function prototypes */
int sandbox_register_driver(struct platform_driver *drv, struct module *mod);
void sandbox_unregister_driver(const char *name);

int sandbox_create_device_sim(struct sandbox_device_sim *sim);
void sandbox_destroy_device_sim(struct sandbox_device_sim *sim);

int sandbox_wrap_file_operations(struct file_operations *fops, 
                                const char *driver_name,
                                struct module *mod);

/* Memory access guards */
unsigned long sandbox_copy_to_user(void __user *to, const void *from, unsigned long n);
unsigned long sandbox_copy_from_user(void *to, const void __user *from, unsigned long n);

/* IRQ simulation */
int sandbox_request_irq(unsigned int irq, irq_handler_t handler,
                       unsigned long flags, const char *name, void *dev);
void sandbox_free_irq(unsigned int irq, void *dev);

/* Logging functions */
void sandbox_log_access(const char *driver_name, const char *operation,
                       const void *addr, size_t size);

/* Utility macros for sandbox drivers */
#define SANDBOX_DRIVER_INIT(name) \
    static int __init sandbox_##name##_init(void) \
    { \
        return sandbox_register_driver(&name##_driver, THIS_MODULE); \
    } \
    module_init(sandbox_##name##_init)

#define SANDBOX_DRIVER_EXIT(name) \
    static void __exit sandbox_##name##_exit(void) \
    { \
        sandbox_unregister_driver(#name); \
    } \
    module_exit(sandbox_##name##_exit)

/* Sandbox driver attribute */
#define SANDBOX_DRIVER_ATTR(name, show, store) \
    static DRIVER_ATTR(name, 0644, show, store)

/* Memory allocation wrappers with tracking */
void *sandbox_kmalloc(size_t size, gfp_t flags, const char *driver_name);
void *sandbox_kzalloc(size_t size, gfp_t flags, const char *driver_name);
void sandbox_kfree(const void *ptr, const char *driver_name);

/* DMA mapping wrappers */
dma_addr_t sandbox_dma_map_single(struct device *dev, void *ptr, size_t size,
                                 enum dma_data_direction direction);
void sandbox_dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size,
                             enum dma_data_direction direction);

/* Register access simulation */
struct sandbox_iomap {
    void __iomem *base;
    resource_size_t size;
    const char *name;
    struct list_head list;
};

void __iomem *sandbox_ioremap(resource_size_t offset, resource_size_t size,
                             const char *driver_name);
void sandbox_iounmap(void __iomem *addr, const char *driver_name);

/* Hardware simulation callbacks */
struct sandbox_hw_sim {
    const char *name;
    int (*init)(struct sandbox_hw_sim *sim);
    void (*cleanup)(struct sandbox_hw_sim *sim);
    int (*read_reg)(struct sandbox_hw_sim *sim, unsigned int reg, unsigned int *val);
    int (*write_reg)(struct sandbox_hw_sim *sim, unsigned int reg, unsigned int val);
    void *private_data;
};

int sandbox_register_hw_sim(struct sandbox_hw_sim *sim);
void sandbox_unregister_hw_sim(const char *name);

/* Panic recovery */
void sandbox_set_panic_handler(void (*handler)(const char *driver_name));

/* Access control */
int sandbox_check_access(const char *driver_name, const char *resource);
void sandbox_restrict_access(const char *driver_name, const char *resource);

#endif /* _SANDBOX_API_H */
