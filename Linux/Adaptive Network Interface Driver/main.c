/**
 * adaptive_net.c - Adaptive Network Interface Driver
 * 
 * A Linux kernel module that implements a network interface which
 * automatically switches from TCP to UDP when network traffic exceeds
 * a configured threshold.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/sock.h>

/* Module information */
MODULE_AUTHOR("Adaptive Network Interface");
MODULE_DESCRIPTION("Network driver that switches between TCP and UDP based on traffic load");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

/* Device name */
#define DEVICE_NAME "adnet0"

/* Traffic monitoring parameters */
#define TRAFFIC_CHECK_INTERVAL (HZ) /* Check traffic every second */
#define DEFAULT_THRESHOLD 10000     /* Packets per second threshold */
#define DEFAULT_COOLDOWN 10         /* Seconds to wait before switching back to TCP */

/* Device structure */
struct adnet_dev {
    struct net_device *dev;
    struct net_device_stats stats;
    
    /* Traffic monitoring */
    unsigned long packet_count;
    unsigned long last_packet_count;
    unsigned long packets_per_second;
    struct timer_list traffic_timer;
    struct workqueue_struct *wq;
    struct work_struct monitor_work;
    
    /* Configuration */
    unsigned long threshold;
    unsigned int cooldown;
    unsigned int cooldown_counter;
    
    /* State */
    bool high_traffic_mode;
    spinlock_t lock;
    
    /* Protocol tracking */
    unsigned long tcp_packets;
    unsigned long udp_packets;
    unsigned long protocol_switch_count;
};

/* Global variables */
static struct proc_dir_entry *adnet_proc_entry;
static struct adnet_dev *g_adnet; /* For procfs access */

/* Forward declarations */
static int adnet_open(struct net_device *dev);
static int adnet_stop(struct net_device *dev);
static netdev_tx_t adnet_xmit(struct sk_buff *skb, struct net_device *dev);
static void adnet_traffic_timer_func(struct timer_list *t);
static void adnet_monitor_work_func(struct work_struct *work);
static int adnet_proc_show(struct seq_file *m, void *v);

/* Network device operations */
static const struct net_device_ops adnet_netdev_ops = {
    .ndo_open = adnet_open,
    .ndo_stop = adnet_stop,
    .ndo_start_xmit = adnet_xmit,
    .ndo_get_stats = NULL, /* Will implement if needed */
};

/**
 * Convert TCP packet to UDP if in high traffic mode
 */
static struct sk_buff *convert_tcp_to_udp(struct sk_buff *skb)
{
    struct iphdr *iph;
    struct tcphdr *tcph;
    struct udphdr *udph;
    int tcphdr_len, iphdr_len;
    unsigned char *payload;
    int payload_len;
    struct sk_buff *new_skb;
    
    if (!skb || skb->len < (sizeof(struct iphdr) + sizeof(struct tcphdr)))
        return NULL;
    
    iph = ip_hdr(skb);
    if (iph->protocol != IPPROTO_TCP)
        return NULL;
        
    iphdr_len = iph->ihl * 4;
    tcph = (struct tcphdr *)((unsigned char *)iph + iphdr_len);
    tcphdr_len = tcph->doff * 4;
    
    /* Get payload */
    payload = (unsigned char *)tcph + tcphdr_len;
    payload_len = ntohs(iph->tot_len) - iphdr_len - tcphdr_len;
    
    /* Create new skb for UDP packet */
    new_skb = dev_alloc_skb(iphdr_len + sizeof(struct udphdr) + payload_len + 16);
    if (!new_skb)
        return NULL;
    
    skb_reserve(new_skb, 16);
    
    /* Copy and modify IP header */
    skb_put(new_skb, iphdr_len + sizeof(struct udphdr) + payload_len);
    memcpy(new_skb->data, iph, iphdr_len);
    
    /* Update IP header */
    iph = (struct iphdr *)new_skb->data;
    iph->protocol = IPPROTO_UDP;
    iph->tot_len = htons(iphdr_len + sizeof(struct udphdr) + payload_len);
    iph->check = 0; /* Will be recalculated in IP stack */
    
    /* Create UDP header */
    udph = (struct udphdr *)(new_skb->data + iphdr_len);
    udph->source = tcph->source;
    udph->dest = tcph->dest;
    udph->len = htons(sizeof(struct udphdr) + payload_len);
    udph->check = 0; /* Optional for IPv4 */
    
    /* Copy payload */
    if (payload_len > 0)
        memcpy(new_skb->data + iphdr_len + sizeof(struct udphdr), payload, payload_len);
    
    /* Set up skb metadata */
    skb_reset_mac_header(new_skb);
    skb_set_network_header(new_skb, 0);
    skb_set_transport_header(new_skb, iphdr_len);
    new_skb->protocol = htons(ETH_P_IP);
    
    return new_skb;
}

/**
 * Initialize the device
 */
static void adnet_setup(struct net_device *dev)
{
    struct adnet_dev *adnet = netdev_priv(dev);
    
    /* Set up device fields */
    ether_setup(dev);
    
    /* Set operations */
    dev->netdev_ops = &adnet_netdev_ops;
    
    /* Generate random MAC address */
    eth_hw_addr_random(dev);
    
    /* Initialize adnet device */
    adnet->dev = dev;
    adnet->threshold = DEFAULT_THRESHOLD;
    adnet->cooldown = DEFAULT_COOLDOWN;
    adnet->cooldown_counter = 0;
    adnet->high_traffic_mode = false;
    adnet->packet_count = 0;
    adnet->last_packet_count = 0;
    adnet->packets_per_second = 0;
    adnet->tcp_packets = 0;
    adnet->udp_packets = 0;
    adnet->protocol_switch_count = 0;
    
    spin_lock_init(&adnet->lock);
    timer_setup(&adnet->traffic_timer, adnet_traffic_timer_func, 0);
}

/**
 * Open the device
 */
static int adnet_open(struct net_device *dev)
{
    struct adnet_dev *adnet = netdev_priv(dev);
    
    netif_start_queue(dev);
    
    /* Initialize work queue */
    adnet->wq = create_singlethread_workqueue("adnet_wq");
    if (!adnet->wq) {
        pr_err("adnet: Failed to create workqueue\n");
        return -ENOMEM;
    }
    
    INIT_WORK(&adnet->monitor_work, adnet_monitor_work_func);
    
    /* Start traffic monitoring */
    mod_timer(&adnet->traffic_timer, jiffies + TRAFFIC_CHECK_INTERVAL);
    
    return 0;
}

/**
 * Stop the device
 */
static int adnet_stop(struct net_device *dev)
{
    struct adnet_dev *adnet = netdev_priv(dev);
    
    netif_stop_queue(dev);
    
    /* Stop traffic monitoring */
    del_timer_sync(&adnet->traffic_timer);
    
    /* Destroy work queue */
    if (adnet->wq) {
        cancel_work_sync(&adnet->monitor_work);
        destroy_workqueue(adnet->wq);
        adnet->wq = NULL;
    }
    
    return 0;
}

/**
 * Transmit a packet
 */
static netdev_tx_t adnet_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct adnet_dev *adnet = netdev_priv(dev);
    struct iphdr *iph;
    struct sk_buff *new_skb = NULL;
    unsigned long flags;
    
    if (skb->protocol == htons(ETH_P_IP)) {
        iph = ip_hdr(skb);
        
        spin_lock_irqsave(&adnet->lock, flags);
        
        /* Count packet for traffic monitoring */
        adnet->packet_count++;
        
        /* Track TCP/UDP packets */
        if (iph->protocol == IPPROTO_TCP) {
            adnet->tcp_packets++;
            
            /* If in high traffic mode, convert TCP to UDP */
            if (adnet->high_traffic_mode) {
                new_skb = convert_tcp_to_udp(skb);
            }
        } else if (iph->protocol == IPPROTO_UDP) {
            adnet->udp_packets++;
        }
        
        spin_unlock_irqrestore(&adnet->lock, flags);
        
        /* If we converted to UDP, use the new skb */
        if (new_skb) {
            dev_kfree_skb(skb);
            skb = new_skb;
        }
    }
    
    /* Forward the packet to the upper layer */
    skb->dev = dev;
    skb->protocol = eth_type_trans(skb, dev);
    
    /* Update stats */
    adnet->stats.tx_packets++;
    adnet->stats.tx_bytes += skb->len;
    
    /* Send to network stack */
    netif_rx(skb);
    
    return NETDEV_TX_OK;
}

/**
 * Traffic monitoring timer function
 */
static void adnet_traffic_timer_func(struct timer_list *t)
{
    struct adnet_dev *adnet = from_timer(adnet, t, traffic_timer);
    
    /* Schedule work to monitor traffic */
    queue_work(adnet->wq, &adnet->monitor_work);
    
    /* Schedule next timer */
    mod_timer(&adnet->traffic_timer, jiffies + TRAFFIC_CHECK_INTERVAL);
}

/**
 * Work function to monitor traffic and adjust behavior
 */
static void adnet_monitor_work_func(struct work_struct *work)
{
    struct adnet_dev *adnet = container_of(work, struct adnet_dev, monitor_work);
    unsigned long flags;
    unsigned long current_count;
    
    spin_lock_irqsave(&adnet->lock, flags);
    
    /* Calculate packets per second */
    current_count = adnet->packet_count;
    adnet->packets_per_second = current_count - adnet->last_packet_count;
    adnet->last_packet_count = current_count;
    
    /* Check if we need to switch modes */
    if (!adnet->high_traffic_mode && adnet->packets_per_second > adnet->threshold) {
        adnet->high_traffic_mode = true;
        adnet->protocol_switch_count++;
        pr_info("adnet: Switching to high traffic mode (UDP) - %lu pps\n", adnet->packets_per_second);
    } else if (adnet->high_traffic_mode) {
        if (adnet->packets_per_second < adnet->threshold) {
            /* Start cooldown counter */
            adnet->cooldown_counter++;
            
            if (adnet->cooldown_counter >= adnet->cooldown) {
                adnet->high_traffic_mode = false;
                adnet->cooldown_counter = 0;
                pr_info("adnet: Switching back to normal mode (TCP) - %lu pps\n", adnet->packets_per_second);
            }
        } else {
            /* Reset cooldown counter */
            adnet->cooldown_counter = 0;
        }
    }
    
    spin_unlock_irqrestore(&adnet->lock, flags);
}

/**
 * Show device information in procfs
 */
static int adnet_proc_show(struct seq_file *m, void *v)
{
    unsigned long flags;
    
    if (!g_adnet)
        return -EINVAL;
    
    spin_lock_irqsave(&g_adnet->lock, flags);
    
    seq_printf(m, "Adaptive Network Interface Statistics:\n");
    seq_printf(m, "Current mode: %s\n", g_adnet->high_traffic_mode ? "High Traffic (UDP)" : "Normal (TCP)");
    seq_printf(m, "Current traffic: %lu packets/second\n", g_adnet->packets_per_second);
    seq_printf(m, "Traffic threshold: %lu packets/second\n", g_adnet->threshold);
    seq_printf(m, "Cooldown period: %u seconds\n", g_adnet->cooldown);
    seq_printf(m, "Cooldown counter: %u/%u\n", g_adnet->cooldown_counter, g_adnet->cooldown);
    seq_printf(m, "Total packets: %lu\n", g_adnet->packet_count);
    seq_printf(m, "TCP packets: %lu\n", g_adnet->tcp_packets);
    seq_printf(m, "UDP packets: %lu\n", g_adnet->udp_packets);
    seq_printf(m, "Protocol switches: %lu\n", g_adnet->protocol_switch_count);
    
    spin_unlock_irqrestore(&g_adnet->lock, flags);
    
    return 0;
}

/**
 * Open procfs entry
 */
static int adnet_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, adnet_proc_show, NULL);
}

/**
 * Procfs file operations
 */
static const struct proc_ops adnet_proc_fops = {
    .proc_open = adnet_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

/**
 * Module initialization
 */
static int __init adnet_init(void)
{
    struct net_device *dev;
    struct adnet_dev *adnet;
    int ret;
    
    /* Allocate network device */
    dev = alloc_netdev(sizeof(struct adnet_dev), DEVICE_NAME, NET_NAME_UNKNOWN, adnet_setup);
    if (!dev) {
        pr_err("adnet: Failed to allocate network device\n");
        return -ENOMEM;
    }
    
    /* Get private data */
    adnet = netdev_priv(dev);
    g_adnet = adnet; /* Save for procfs access */
    
    /* Register network device */
    ret = register_netdev(dev);
    if (ret) {
        pr_err("adnet: Failed to register network device: %d\n", ret);
        free_netdev(dev);
        return ret;
    }
    
    /* Create proc entry */
    adnet_proc_entry = proc_create("adnet", 0644, NULL, &adnet_proc_fops);
    if (!adnet_proc_entry) {
        pr_err("adnet: Failed to create proc entry\n");
        unregister_netdev(dev);
        free_netdev(dev);
        return -ENOMEM;
    }
    
    pr_info("adnet: Adaptive Network Interface loaded\n");
    
    return 0;
}

/**
 * Module cleanup
 */
static void __exit adnet_exit(void)
{
    if (g_adnet) {
        /* Remove proc entry */
        if (adnet_proc_entry)
            proc_remove(adnet_proc_entry);
            
        /* Unregister and free device */
        unregister_netdev(g_adnet->dev);
        free_netdev(g_adnet->dev);
        g_adnet = NULL;
    }
    
    pr_info("adnet: Adaptive Network Interface unloaded\n");
}

module_init(adnet_init);
module_exit(adnet_exit);
