/*
 * Ephemeral IP Tunneling Driver with Dynamic IP Assignment
 * For Linux systems
 * 
 * This driver creates a virtual network interface that establishes temporary
 * tunnels with dynamically assigned IP addresses.
 * 
 * Compile with:
 *   gcc -o eptunnel eptunnel.c -lrt
 * 
 * Run with:
 *   sudo ./eptunnel
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define MAX_PACKET_SIZE 4096
#define IP_POOL_SIZE 254
#define TUNNEL_TTL 300  // Time in seconds before tunnel expires
#define DEFAULT_MTU 1500
#define INTERFACE_NAME "eptun0"

// Structure to hold tunnel information
typedef struct {
    char remote_addr[INET_ADDRSTRLEN];
    uint16_t remote_port;
    struct in_addr ip_addr;
    time_t creation_time;
    int active;
} tunnel_info;

// Global variables
int tun_fd = -1;
int sock_fd = -1;
tunnel_info tunnels[IP_POOL_SIZE];
pthread_mutex_t tunnels_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int keep_running = 1;

// Signal handler for graceful termination
void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        printf("\nReceived termination signal. Shutting down...\n");
        keep_running = 0;
    }
}

// Function to allocate TUN device
int tun_alloc(char *dev_name) {
    struct ifreq ifr;
    int fd, err;

    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
        perror("Error opening /dev/net/tun");
        return fd;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;  // TUN device without packet info

    if (*dev_name)
        strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);

    if ((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0) {
        perror("ioctl(TUNSETIFF) failed");
        close(fd);
        return err;
    }

    strcpy(dev_name, ifr.ifr_name);
    return fd;
}

// Function to set up the TUN interface
int setup_tun_interface(char *dev_name) {
    char cmd[256];
    
    // Set interface up
    snprintf(cmd, sizeof(cmd), "ip link set dev %s up", dev_name);
    if (system(cmd) != 0) {
        perror("Failed to bring up interface");
        return -1;
    }
    
    // Set MTU
    snprintf(cmd, sizeof(cmd), "ip link set dev %s mtu %d", dev_name, DEFAULT_MTU);
    if (system(cmd) != 0) {
        perror("Failed to set MTU");
        return -1;
    }
    
    // Set interface address to 10.0.0.1/24
    snprintf(cmd, sizeof(cmd), "ip addr add 10.0.0.1/24 dev %s", dev_name);
    if (system(cmd) != 0) {
        perror("Failed to set interface address");
        return -1;
    }
    
    return 0;
}

// Function to dynamically assign an IP
struct in_addr assign_dynamic_ip() {
    struct in_addr ip;
    int assigned = 0;
    time_t current_time = time(NULL);
    
    pthread_mutex_lock(&tunnels_mutex);
    
    // Look for expired tunnels first
    for (int i = 0; i < IP_POOL_SIZE; i++) {
        if (tunnels[i].active && difftime(current_time, tunnels[i].creation_time) > TUNNEL_TTL) {
            // Expired tunnel, mark as inactive
            tunnels[i].active = 0;
        }
    }
    
    // Find an inactive slot
    for (int i = 0; i < IP_POOL_SIZE; i++) {
        if (!tunnels[i].active) {
            // Construct IP address 10.0.0.X where X is i+2 (avoid .0 and .1)
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), "10.0.0.%d", i + 2);
            inet_pton(AF_INET, ip_str, &ip);
            
            assigned = 1;
            break;
        }
    }
    
    pthread_mutex_unlock(&tunnels_mutex);
    
    if (!assigned) {
        // No IPs available, return 0.0.0.0
        inet_pton(AF_INET, "0.0.0.0", &ip);
    }
    
    return ip;
}

// Function to register a new tunnel
int register_tunnel(const char *remote_addr, uint16_t remote_port, struct in_addr ip_addr) {
    int ip_last_octet = ntohl(ip_addr.s_addr) & 0xFF;
    int index = ip_last_octet - 2;  // Convert back to index
    
    if (index < 0 || index >= IP_POOL_SIZE) {
        return -1;
    }
    
    pthread_mutex_lock(&tunnels_mutex);
    
    strncpy(tunnels[index].remote_addr, remote_addr, INET_ADDRSTRLEN);
    tunnels[index].remote_port = remote_port;
    tunnels[index].ip_addr = ip_addr;
    tunnels[index].creation_time = time(NULL);
    tunnels[index].active = 1;
    
    pthread_mutex_unlock(&tunnels_mutex);
    
    return 0;
}

// Function to find a tunnel by IP
int find_tunnel_by_ip(struct in_addr ip) {
    int ip_last_octet = ntohl(ip.s_addr) & 0xFF;
    int index = ip_last_octet - 2;  // Convert back to index
    
    if (index < 0 || index >= IP_POOL_SIZE) {
        return -1;
    }
    
    pthread_mutex_lock(&tunnels_mutex);
    int active = tunnels[index].active;
    pthread_mutex_unlock(&tunnels_mutex);
    
    return active ? index : -1;
}

// Function to find a tunnel by remote address and port
int find_tunnel_by_remote(const char *remote_addr, uint16_t remote_port) {
    int found_index = -1;
    
    pthread_mutex_lock(&tunnels_mutex);
    
    for (int i = 0; i < IP_POOL_SIZE; i++) {
        if (tunnels[i].active && 
            strcmp(tunnels[i].remote_addr, remote_addr) == 0 && 
            tunnels[i].remote_port == remote_port) {
            found_index = i;
            break;
        }
    }
    
    pthread_mutex_unlock(&tunnels_mutex);
    
    return found_index;
}

// Thread to clean up expired tunnels
void *tunnel_cleanup_thread(void *arg) {
    while (keep_running) {
        time_t current_time = time(NULL);
        
        pthread_mutex_lock(&tunnels_mutex);
        
        for (int i = 0; i < IP_POOL_SIZE; i++) {
            if (tunnels[i].active && difftime(current_time, tunnels[i].creation_time) > TUNNEL_TTL) {
                printf("Tunnel to %s:%d expired (IP: 10.0.0.%d)\n", 
                       tunnels[i].remote_addr, 
                       tunnels[i].remote_port, 
                       i + 2);
                tunnels[i].active = 0;
            }
        }
        
        pthread_mutex_unlock(&tunnels_mutex);
        
        // Sleep for 10 seconds before next cleanup
        sleep(10);
    }
    
    return NULL;
}

// Handle packets from TUN device
void *tun_reader_thread(void *arg) {
    unsigned char buffer[MAX_PACKET_SIZE];
    struct sockaddr_in remote_addr;
    int bytes_read;
    
    while (keep_running) {
        // Read packet from TUN device
        bytes_read = read(tun_fd, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            perror("Error reading from TUN device");
            continue;
        }
        
        // Process IP packet (simple version, assumes IPv4)
        if (bytes_read > 20) {  // Minimum IPv4 header size
            struct in_addr dst_ip;
            memcpy(&dst_ip.s_addr, buffer + 16, 4);  // Destination IP from IPv4 header
            
            // Find the tunnel for this destination
            int tunnel_index = find_tunnel_by_ip(dst_ip);
            if (tunnel_index >= 0) {
                pthread_mutex_lock(&tunnels_mutex);
                
                // Update the tunnel's timestamp
                tunnels[tunnel_index].creation_time = time(NULL);
                
                // Set up destination socket address
                remote_addr.sin_family = AF_INET;
                remote_addr.sin_port = htons(tunnels[tunnel_index].remote_port);
                inet_pton(AF_INET, tunnels[tunnel_index].remote_addr, &remote_addr.sin_addr);
                
                pthread_mutex_unlock(&tunnels_mutex);
                
                // Send the packet to the remote endpoint
                sendto(sock_fd, buffer, bytes_read, 0, 
                       (struct sockaddr *)&remote_addr, sizeof(remote_addr));
            }
        }
    }
    
    return NULL;
}

// Handle packets from UDP socket
void *udp_reader_thread(void *arg) {
    unsigned char buffer[MAX_PACKET_SIZE];
    struct sockaddr_in remote_addr;
    socklen_t addrlen = sizeof(remote_addr);
    int bytes_read;
    char remote_addr_str[INET_ADDRSTRLEN];
    
    while (keep_running) {
        // Read packet from UDP socket
        bytes_read = recvfrom(sock_fd, buffer, sizeof(buffer), 0,
                              (struct sockaddr *)&remote_addr, &addrlen);
        if (bytes_read < 0) {
            perror("Error reading from UDP socket");
            continue;
        }
        
        inet_ntop(AF_INET, &remote_addr.sin_addr, remote_addr_str, INET_ADDRSTRLEN);
        
        // Check if this is a new connection or existing tunnel
        int tunnel_index = find_tunnel_by_remote(remote_addr_str, ntohs(remote_addr.sin_port));
        
        if (tunnel_index < 0) {
            // New connection, assign IP
            struct in_addr assigned_ip = assign_dynamic_ip();
            
            // Skip if no IP available
            if (assigned_ip.s_addr == 0) {
                continue;
            }
            
            // Register the new tunnel
            register_tunnel(remote_addr_str, ntohs(remote_addr.sin_port), assigned_ip);
            
            // Log the new tunnel
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &assigned_ip, ip_str, INET_ADDRSTRLEN);
            printf("New tunnel established: %s:%d -> %s\n", 
                   remote_addr_str, ntohs(remote_addr.sin_port), ip_str);
        } else {
            // Update existing tunnel's timestamp
            pthread_mutex_lock(&tunnels_mutex);
            tunnels[tunnel_index].creation_time = time(NULL);
            pthread_mutex_unlock(&tunnels_mutex);
        }
        
        // Write the packet to the TUN device
        write(tun_fd, buffer, bytes_read);
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    char tun_name[IFNAMSIZ] = INTERFACE_NAME;
    pthread_t tun_thread, udp_thread, cleanup_thread;
    struct sockaddr_in local_addr;
    int port = 51820;  // Default port, similar to WireGuard
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            strncpy(tun_name, argv[i + 1], IFNAMSIZ - 1);
            i++;
        } else {
            printf("Usage: %s [-p port] [-i interface_name]\n", argv[0]);
            return 1;
        }
    }
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize tunnels array
    memset(tunnels, 0, sizeof(tunnels));
    
    // Allocate TUN device
    if ((tun_fd = tun_alloc(tun_name)) < 0) {
        fprintf(stderr, "Failed to allocate TUN device\n");
        return 1;
    }
    
    printf("TUN device %s allocated\n", tun_name);
    
    // Set up TUN interface
    if (setup_tun_interface(tun_name) < 0) {
        fprintf(stderr, "Failed to set up TUN interface\n");
        close(tun_fd);
        return 1;
    }
    
    // Create UDP socket
    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Failed to create UDP socket");
        close(tun_fd);
        return 1;
    }
    
    // Bind UDP socket
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(port);
    
    if (bind(sock_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("Failed to bind UDP socket");
        close(sock_fd);
        close(tun_fd);
        return 1;
    }
    
    printf("UDP socket bound to port %d\n", port);
    
    // Create threads
    if (pthread_create(&tun_thread, NULL, tun_reader_thread, NULL) != 0) {
        perror("Failed to create TUN reader thread");
        close(sock_fd);
        close(tun_fd);
        return 1;
    }
    
    if (pthread_create(&udp_thread, NULL, udp_reader_thread, NULL) != 0) {
        perror("Failed to create UDP reader thread");
        keep_running = 0;
        pthread_join(tun_thread, NULL);
        close(sock_fd);
        close(tun_fd);
        return 1;
    }
    
    if (pthread_create(&cleanup_thread, NULL, tunnel_cleanup_thread, NULL) != 0) {
        perror("Failed to create cleanup thread");
        keep_running = 0;
        pthread_join(tun_thread, NULL);
        pthread_join(udp_thread, NULL);
        close(sock_fd);
        close(tun_fd);
        return 1;
    }
    
    printf("Ephemeral IP Tunneling Driver started\n");
    printf("Press Ctrl+C to stop\n");
    
    // Wait for threads to complete
    pthread_join(tun_thread, NULL);
    pthread_join(udp_thread, NULL);
    pthread_join(cleanup_thread, NULL);
    
    // Clean up
    close(sock_fd);
    close(tun_fd);
    
    // Remove the TUN interface
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip link delete %s", tun_name);
    system(cmd);
    
    printf("Ephemeral IP Tunneling Driver stopped\n");
    
    return 0;
}
