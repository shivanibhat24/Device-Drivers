/**
 * interrupt_latency_benchmark.c - High-Resolution Latency Benchmark Driver
 * 
 * This driver measures and logs nanosecond-level latency for interrupt handlers,
 * providing detailed timing information and detecting latency spikes.
 *
 * Author: Shivani
 * Date: April 21, 2025
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/atomic.h>
#include <linux/percpu.h>
#include <linux/version.h>

#define BENCH_DRIVER_NAME "latency_benchmark"
#define PROC_ENTRY_NAME "latency_stats"
#define NS_PER_SEC 1000000000ULL
#define DEFAULT_THRESHOLD_NS 50000  /* 50 microseconds default threshold */
#define DEFAULT_SAMPLE_PERIOD_NS 100000  /* 100 microseconds between measurements */
#define MAX_LATENCY_RECORDS 1000
#define MAX_IRQ_NAME_LEN 32

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shivani");
MODULE_DESCRIPTION("High-Resolution Interrupt Latency Benchmark Driver");
MODULE_VERSION("1.0");

/* Module parameters */
static unsigned long threshold_ns = DEFAULT_THRESHOLD_NS;
module_param(threshold_ns, ulong, 0644);
MODULE_PARM_DESC(threshold_ns, "Latency threshold in nanoseconds to report as a spike");

static unsigned long sample_period_ns = DEFAULT_SAMPLE_PERIOD_NS;
module_param(sample_period_ns, ulong, 0644);
MODULE_PARM_DESC(sample_period_ns, "Period between latency measurements in nanoseconds");

static int target_irq = -1;  /* -1 means measure all IRQs */
module_param(target_irq, int, 0644);
MODULE_PARM_DESC(target_irq, "Specific IRQ number to monitor (-1 for all IRQs)");

/* Structure to store latency records */
struct latency_record {
    ktime_t timestamp;       /* When the latency was measured */
    u64 latency_ns;          /* Measured latency in nanoseconds */
    int irq;                 /* IRQ number */
    char irq_name[MAX_IRQ_NAME_LEN]; /* IRQ name/description */
    unsigned int cpu;        /* CPU where interrupt occurred */
    atomic_t in_use;         /* Is this record in use? */
};

struct cpu_latency_tracking {
    ktime_t last_timestamp;
    ktime_t irq_entry_time;
    atomic_t inside_irq;
};

/* Global variables */
static struct proc_dir_entry *proc_entry;
static struct latency_record *latency_records;
static atomic_t record_index = ATOMIC_INIT(0);
static atomic_t total_records = ATOMIC_INIT(0);
static struct task_struct *periodic_task;
static DEFINE_SPINLOCK(records_lock);

/* Per-CPU tracking data */
static DEFINE_PER_CPU(struct cpu_latency_tracking, cpu_tracking);

/* Interrupt entry hook for latency measurement */
void latency_irq_entry_hook(int irq) {
    struct cpu_latency_tracking *tracking;
    
    if (target_irq != -1 && irq != target_irq)
        return;
    
    tracking = this_cpu_ptr(&cpu_tracking);
    
    if (atomic_read(&tracking->inside_irq) == 0) {
        tracking->irq_entry_time = ktime_get();
        atomic_set(&tracking->inside_irq, 1);
    }
}

/* Interrupt exit hook for latency measurement */
void latency_irq_exit_hook(int irq) {
    struct cpu_latency_tracking *tracking;
    ktime_t exit_time, delta;
    u64 latency_ns;
    struct latency_record *record;
    int idx;
    unsigned int cpu = smp_processor_id();
    
    if (target_irq != -1 && irq != target_irq)
        return;
    
    tracking = this_cpu_ptr(&cpu_tracking);
    
    if (atomic_read(&tracking->inside_irq) == 1) {
        exit_time = ktime_get();
        delta = ktime_sub(exit_time, tracking->irq_entry_time);
        latency_ns = ktime_to_ns(delta);
        atomic_set(&tracking->inside_irq, 0);
        
        /* Only record if we exceed the threshold */
        if (latency_ns > threshold_ns) {
            /* Get next record slot */
            idx = atomic_inc_return(&record_index) % MAX_LATENCY_RECORDS;
            record = &latency_records[idx];
            
            /* Claim this record */
            if (atomic_cmpxchg(&record->in_use, 0, 1) == 0) {
                record->timestamp = exit_time;
                record->latency_ns = latency_ns;
                record->irq = irq;
                record->cpu = cpu;
                
                /* Get IRQ name */
                {
                    const char *name = irq_to_desc(irq) ? 
                        (irq_to_desc(irq)->action ? 
                         (irq_to_desc(irq)->action->name ? : "unknown") : "none") : "invalid";
                    
                    strncpy(record->irq_name, name, MAX_IRQ_NAME_LEN - 1);
                    record->irq_name[MAX_IRQ_NAME_LEN - 1] = '\0';
                }
                
                atomic_inc(&total_records);
                atomic_set(&record->in_use, 0);
            }
        }
    }
}

/* Hooks for the IRQ handler entry/exit points */
static struct irq_hook {
    void (*entry)(int irq);
    void (*exit)(int irq);
} irq_hooks = {
    .entry = latency_irq_entry_hook,
    .exit = latency_irq_exit_hook,
};

/* Register our hooks with the kernel's IRQ system */
static int register_irq_hooks(void) {
    /* This is a simplified version - in a real driver, you would need to
     * use appropriate kernel APIs or patch the kernel to hook IRQ entry/exit points.
     * For Linux 5.7+, you might use the trace events system or kprobes.
     */
    printk(KERN_INFO "%s: IRQ hooks would be registered here in a real implementation\n", 
           BENCH_DRIVER_NAME);
    return 0;
}

static void unregister_irq_hooks(void) {
    /* Cleanup any hooks registered above */
    printk(KERN_INFO "%s: IRQ hooks would be unregistered here\n", BENCH_DRIVER_NAME);
}

/* Process the latency records periodically */
static int periodic_checker(void *data) {
    struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };
    int old_prio = current->prio;
    int old_policy = current->policy;
    u64 max_ns = 0;
    int max_irq = -1;
    unsigned int max_cpu = 0;
    
    /* Set this thread to real-time priority */
    sched_setscheduler(current, SCHED_FIFO, &param);
    
    while (!kthread_should_stop()) {
        int i;
        max_ns = 0;
        
        /* Sleep for our sampling period */
        usleep_range(sample_period_ns / 1000, sample_period_ns / 1000 + 10);
        
        /* Check for highest latency in the last period */
        spin_lock(&records_lock);
        for (i = 0; i < MAX_LATENCY_RECORDS; i++) {
            if (latency_records[i].latency_ns > max_ns) {
                max_ns = latency_records[i].latency_ns;
                max_irq = latency_records[i].irq;
                max_cpu = latency_records[i].cpu;
            }
        }
        spin_unlock(&records_lock);
        
        /* Report any significant spikes immediately */
        if (max_ns > threshold_ns * 5) {
            printk(KERN_ALERT "%s: High latency detected! IRQ %d on CPU %u: %llu ns\n",
                   BENCH_DRIVER_NAME, max_irq, max_cpu, max_ns);
        }
    }
    
    /* Restore old priority settings before exiting */
    param.sched_priority = old_prio;
    sched_setscheduler(current, old_policy, &param);
    
    return 0;
}

/* Proc file operations for reporting latency statistics */
static int latency_proc_show(struct seq_file *m, void *v) {
    int i;
    int count = atomic_read(&total_records);
    int count_to_show = min(count, MAX_LATENCY_RECORDS);
    u64 total_latency = 0, avg_latency = 0;
    u64 min_latency = ULLONG_MAX, max_latency = 0;
    
    seq_printf(m, "Interrupt Latency Benchmark Statistics\n");
    seq_printf(m, "-------------------------------------\n");
    seq_printf(m, "Threshold: %lu ns, Sample Period: %lu ns\n", 
               threshold_ns, sample_period_ns);
    seq_printf(m, "Target IRQ: %d (%s)\n", target_irq, 
               target_irq == -1 ? "all IRQs" : "specific IRQ");
    seq_printf(m, "Total latency records: %d\n\n", count);
    
    spin_lock(&records_lock);
    
    /* Calculate summary statistics */
    for (i = 0; i < MAX_LATENCY_RECORDS; i++) {
        if (latency_records[i].latency_ns > 0) {
            total_latency += latency_records[i].latency_ns;
            
            if (latency_records[i].latency_ns < min_latency)
                min_latency = latency_records[i].latency_ns;
                
            if (latency_records[i].latency_ns > max_latency)
                max_latency = latency_records[i].latency_ns;
        }
    }
    
    if (count_to_show > 0) {
        avg_latency = total_latency / count_to_show;
        
        seq_printf(m, "Min latency: %llu ns\n", min_latency);
        seq_printf(m, "Avg latency: %llu ns\n", avg_latency);
        seq_printf(m, "Max latency: %llu ns\n\n", max_latency);
        
        /* Table header */
        seq_printf(m, "%-16s %-8s %-32s %-8s %-16s\n", 
                   "Timestamp(s)", "IRQ", "Name", "CPU", "Latency(ns)");
        seq_printf(m, "-----------------------------------------------------------------------\n");
        
        /* Show detailed records */
        for (i = 0; i < MAX_LATENCY_RECORDS; i++) {
            if (latency_records[i].latency_ns > 0) {
                seq_printf(m, "%-16lld %-8d %-32s %-8u %-16llu\n",
                           ktime_to_ns(latency_records[i].timestamp) / NS_PER_SEC,
                           latency_records[i].irq,
                           latency_records[i].irq_name,
                           latency_records[i].cpu,
                           latency_records[i].latency_ns);
            }
        }
    } else {
        seq_printf(m, "No latency records collected yet.\n");
    }
    
    spin_unlock(&records_lock);
    return 0;
}

static int latency_proc_open(struct inode *inode, struct file *file) {
    return single_open(file, latency_proc_show, NULL);
}

static const struct proc_ops latency_proc_fops = {
    .proc_open = latency_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

/* Debug functions */
static void print_debug_info(void) {
    printk(KERN_INFO "%s: Debug info - Current kernel time: %lld ns\n", 
           BENCH_DRIVER_NAME, ktime_to_ns(ktime_get()));
    printk(KERN_INFO "%s: CPU frequency: %lu MHz\n", 
           BENCH_DRIVER_NAME, cpufreq_quick_get(0) / 1000);
    printk(KERN_INFO "%s: Current CPU: %d\n", BENCH_DRIVER_NAME, smp_processor_id());
    printk(KERN_INFO "%s: Current IRQ count: %u\n", 
           BENCH_DRIVER_NAME, kstat_irqs_usr(0));  /* 0 means sum across all CPUs */
}

/* Module initialization */
static int __init latency_benchmark_init(void) {
    int cpu;
    
    printk(KERN_INFO "%s: Initializing high-resolution interrupt latency benchmark\n", 
           BENCH_DRIVER_NAME);
    
    /* Allocate memory for latency records */
    latency_records = kzalloc(sizeof(struct latency_record) * MAX_LATENCY_RECORDS, GFP_KERNEL);
    if (!latency_records) {
        printk(KERN_ERR "%s: Failed to allocate memory for latency records\n", 
               BENCH_DRIVER_NAME);
        return -ENOMEM;
    }
    
    /* Initialize per-CPU data */
    for_each_possible_cpu(cpu) {
        struct cpu_latency_tracking *tracking = per_cpu_ptr(&cpu_tracking, cpu);
        tracking->last_timestamp = ktime_get();
        atomic_set(&tracking->inside_irq, 0);
    }
    
    /* Initialize latency records */
    for (cpu = 0; cpu < MAX_LATENCY_RECORDS; cpu++) {
        atomic_set(&latency_records[cpu].in_use, 0);
    }
    
    /* Create proc file entry */
    proc_entry = proc_create(PROC_ENTRY_NAME, 0444, NULL, &latency_proc_fops);
    if (!proc_entry) {
        printk(KERN_ERR "%s: Failed to create proc entry\n", BENCH_DRIVER_NAME);
        kfree(latency_records);
        return -ENOMEM;
    }
    
    /* Register IRQ hooks */
    if (register_irq_hooks() != 0) {
        printk(KERN_ERR "%s: Failed to register IRQ hooks\n", BENCH_DRIVER_NAME);
        remove_proc_entry(PROC_ENTRY_NAME, NULL);
        kfree(latency_records);
        return -EINVAL;
    }
    
    /* Create and start periodic checking thread */
    periodic_task = kthread_create(periodic_checker, NULL, "latency_checker");
    if (IS_ERR(periodic_task)) {
        printk(KERN_ERR "%s: Failed to create periodic checker thread\n", 
               BENCH_DRIVER_NAME);
        unregister_irq_hooks();
        remove_proc_entry(PROC_ENTRY_NAME, NULL);
        kfree(latency_records);
        return PTR_ERR(periodic_task);
    }
    wake_up_process(periodic_task);
    
    /* Print debug info */
    print_debug_info();
    
    printk(KERN_INFO "%s: Initialization complete\n", BENCH_DRIVER_NAME);
    return 0;
}

/* Module cleanup */
static void __exit latency_benchmark_exit(void) {
    printk(KERN_INFO "%s: Shutting down\n", BENCH_DRIVER_NAME);
    
    /* Stop the periodic checker thread */
    if (periodic_task) {
        kthread_stop(periodic_task);
    }
    
    /* Unregister IRQ hooks */
    unregister_irq_hooks();
    
    /* Remove proc entry */
    if (proc_entry) {
        remove_proc_entry(PROC_ENTRY_NAME, NULL);
    }
    
    /* Free allocated memory */
    kfree(latency_records);
    
    printk(KERN_INFO "%s: Cleanup complete\n", BENCH_DRIVER_NAME);
}

module_init(latency_benchmark_init);
module_exit(latency_benchmark_exit);
