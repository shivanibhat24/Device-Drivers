/**
 * Minimal Microkernel
 * Features:
 * - CPU bootstrapping
 * - Task isolation
 * - Message-passing IPC
 */

#include <stdint.h>
#include <stdbool.h>

/* Memory layout and constants */
#define KERNEL_VIRTUAL_BASE 0xC0000000
#define MAX_TASKS 64
#define MAX_MESSAGES 256
#define MAX_MESSAGE_SIZE 1024
#define STACK_SIZE 4096

/* Hardware abstraction types */
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip, eflags;
    uint32_t cs, ds, es, fs, gs, ss;
} cpu_context_t;

/* Memory management structures */
typedef struct {
    uint32_t* page_directory;
    void* heap_start;
    size_t heap_size;
} memory_context_t;

/* Message passing structures */
typedef enum {
    MSG_NONE = 0,
    MSG_CREATE_TASK,
    MSG_KILL_TASK,
    MSG_USER_DEFINED
} message_type_t;

typedef struct {
    message_type_t type;
    uint32_t sender;
    uint32_t receiver;
    uint32_t size;
    uint8_t data[MAX_MESSAGE_SIZE];
    bool is_replied;
    uint32_t reply_id;
} message_t;

/* Task control block */
typedef enum {
    TASK_UNUSED = 0,
    TASK_CREATED,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

typedef struct {
    uint32_t id;
    task_state_t state;
    cpu_context_t context;
    memory_context_t memory;
    uint32_t* kernel_stack;
    uint32_t* user_stack;
    uint32_t pending_message;
    uint32_t waiting_for_message;
} task_t;

/* Global kernel state */
static task_t tasks[MAX_TASKS];
static message_t messages[MAX_MESSAGES];
static uint32_t current_task_id;
static uint32_t next_message_id;

/* Forward declarations */
void kernel_main(void);
uint32_t task_create(void (*entry_point)(void));
void task_schedule(void);
uint32_t send_message(uint32_t receiver, message_type_t type, void* data, uint32_t size);
bool receive_message(uint32_t* sender, message_type_t* type, void* data, uint32_t* size);
void reply_message(uint32_t message_id, void* reply_data, uint32_t reply_size);

/* Assembly interface functions */
extern void cpu_jump_usermode(uint32_t entry, uint32_t stack);
extern void cpu_context_switch(cpu_context_t* old_context, cpu_context_t* new_context);
extern void cpu_enable_interrupts(void);
extern void cpu_disable_interrupts(void);
extern void cpu_halt(void);

/* Low-level initialization code */
void bootstrap(void) {
    // Initialize CPU (GDT, IDT setup would go here)
    
    // Initialize physical memory manager
    
    // Setup virtual memory (paging)
    
    // Initialize kernel heap
    
    // Initialize IPC system
    for (int i = 0; i < MAX_MESSAGES; i++) {
        messages[i].type = MSG_NONE;
    }
    
    // Initialize task structures
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_UNUSED;
    }
    
    // Create the first kernel task
    current_task_id = 0;
    tasks[0].id = 0;
    tasks[0].state = TASK_RUNNING;
    
    // Jump to kernel main
    kernel_main();
}

/* Memory management functions */
void* kmalloc(size_t size) {
    // Simple implementation would go here
    return NULL; // Placeholder
}

void kfree(void* ptr) {
    // Simple implementation would go here
}

memory_context_t* create_memory_context(void) {
    memory_context_t* context = kmalloc(sizeof(memory_context_t));
    if (!context) return NULL;
    
    // Allocate page directory
    context->page_directory = kmalloc(4096);
    if (!context->page_directory) {
        kfree(context);
        return NULL;
    }
    
    // Map kernel space (identity mapping for kernel)
    
    // Set up initial heap
    context->heap_start = kmalloc(4096);
    context->heap_size = 4096;
    
    return context;
}

/* Task management */
uint32_t task_create(void (*entry_point)(void)) {
    cpu_disable_interrupts();
    
    // Find free task slot
    uint32_t new_id = 0;
    for (new_id = 0; new_id < MAX_TASKS; new_id++) {
        if (tasks[new_id].state == TASK_UNUSED)
            break;
    }
    
    if (new_id >= MAX_TASKS) {
        cpu_enable_interrupts();
        return (uint32_t)-1; // No free slots
    }
    
    // Initialize task
    tasks[new_id].id = new_id;
    tasks[new_id].state = TASK_CREATED;
    tasks[new_id].pending_message = (uint32_t)-1;
    tasks[new_id].waiting_for_message = 0;
    
    // Allocate kernel and user stacks
    tasks[new_id].kernel_stack = kmalloc(STACK_SIZE);
    tasks[new_id].user_stack = kmalloc(STACK_SIZE);
    
    if (!tasks[new_id].kernel_stack || !tasks[new_id].user_stack) {
        if (tasks[new_id].kernel_stack) kfree(tasks[new_id].kernel_stack);
        if (tasks[new_id].user_stack) kfree(tasks[new_id].user_stack);
        tasks[new_id].state = TASK_UNUSED;
        cpu_enable_interrupts();
        return (uint32_t)-1;
    }
    
    // Create memory context
    memory_context_t* mem_ctx = create_memory_context();
    if (!mem_ctx) {
        kfree(tasks[new_id].kernel_stack);
        kfree(tasks[new_id].user_stack);
        tasks[new_id].state = TASK_UNUSED;
        cpu_enable_interrupts();
        return (uint32_t)-1;
    }
    
    tasks[new_id].memory = *mem_ctx;
    kfree(mem_ctx);
    
    // Setup initial CPU context
    tasks[new_id].context.eip = (uint32_t)entry_point;
    tasks[new_id].context.esp = (uint32_t)tasks[new_id].user_stack + STACK_SIZE - 4;
    tasks[new_id].context.eflags = 0x202; // IF=1
    tasks[new_id].context.cs = 0x1B;      // User code segment with RPL=3
    tasks[new_id].context.ds = 0x23;      // User data segment with RPL=3
    tasks[new_id].context.es = 0x23;
    tasks[new_id].context.fs = 0x23;
    tasks[new_id].context.gs = 0x23;
    tasks[new_id].context.ss = 0x23;
    
    tasks[new_id].state = TASK_READY;
    
    cpu_enable_interrupts();
    return new_id;
}

void task_schedule(void) {
    if (current_task_id >= MAX_TASKS)
        current_task_id = 0;
        
    task_t* current = &tasks[current_task_id];
    
    // Save current context if task is running
    if (current->state == TASK_RUNNING) {
        current->state = TASK_READY;
    }
    
    // Find next runnable task
    uint32_t next_id = current_task_id;
    do {
        next_id = (next_id + 1) % MAX_TASKS;
        if (next_id == current_task_id) {
            // If we went full circle, just continue with current task
            break;
        }
    } while (tasks[next_id].state != TASK_READY);
    
    // If no tasks ready, idle
    if (tasks[next_id].state != TASK_READY) {
        cpu_halt();
        return;
    }
    
    task_t* next = &tasks[next_id];
    next->state = TASK_RUNNING;
    
    // Switch memory context if needed
    if (current_task_id != next_id) {
        // Switch page directory to next task's
        
        // Perform context switch
        cpu_context_switch(&current->context, &next->context);
        current_task_id = next_id;
    }
}

/* IPC Message passing */
uint32_t send_message(uint32_t receiver, message_type_t type, void* data, uint32_t size) {
    if (size > MAX_MESSAGE_SIZE) {
        return (uint32_t)-1;
    }
    
    cpu_disable_interrupts();
    
    // Find free message slot
    uint32_t msg_id = next_message_id;
    uint32_t start_id = msg_id;
    
    do {
        if (messages[msg_id].type == MSG_NONE)
            break;
        
        msg_id = (msg_id + 1) % MAX_MESSAGES;
        if (msg_id == start_id) {
            cpu_enable_interrupts();
            return (uint32_t)-1; // No free message slots
        }
    } while (1);
    
    // Initialize message
    messages[msg_id].type = type;
    messages[msg_id].sender = current_task_id;
    messages[msg_id].receiver = receiver;
    messages[msg_id].size = size;
    messages[msg_id].is_replied = false;
    
    if (data && size > 0) {
        for (uint32_t i = 0; i < size; i++) {
            messages[msg_id].data[i] = ((uint8_t*)data)[i];
        }
    }
    
    // Update next message ID
    next_message_id = (msg_id + 1) % MAX_MESSAGES;
    
    // If receiver is waiting for message, unblock it
    if (tasks[receiver].state == TASK_BLOCKED && 
        tasks[receiver].waiting_for_message) {
        
        tasks[receiver].pending_message = msg_id;
        tasks[receiver].waiting_for_message = 0;
        tasks[receiver].state = TASK_READY;
    } else {
        // Otherwise mark message as pending
        tasks[receiver].pending_message = msg_id;
    }
    
    cpu_enable_interrupts();
    return msg_id;
}

bool receive_message(uint32_t* sender, message_type_t* type, void* data, uint32_t* size) {
    cpu_disable_interrupts();
    
    if (tasks[current_task_id].pending_message != (uint32_t)-1) {
        // Message waiting, process it
        uint32_t msg_id = tasks[current_task_id].pending_message;
        message_t* msg = &messages[msg_id];
        
        *sender = msg->sender;
        *type = msg->type;
        *size = (msg->size > MAX_MESSAGE_SIZE) ? MAX_MESSAGE_SIZE : msg->size;
        
        for (uint32_t i = 0; i < *size; i++) {
            ((uint8_t*)data)[i] = msg->data[i];
        }
        
        // Clear pending message
        tasks[current_task_id].pending_message = (uint32_t)-1;
        
        // Don't free message yet - keep for reply
        
        cpu_enable_interrupts();
        return true;
    }
    
    // No message waiting, block
    tasks[current_task_id].waiting_for_message = 1;
    tasks[current_task_id].state = TASK_BLOCKED;
    
    cpu_enable_interrupts();
    
    // Schedule next task
    task_schedule();
    
    // When we get here, a message has been received
    return receive_message(sender, type, data, size);
}

void reply_message(uint32_t message_id, void* reply_data, uint32_t reply_size) {
    if (message_id >= MAX_MESSAGES || messages[message_id].type == MSG_NONE) {
        return; // Invalid message ID
    }
    
    cpu_disable_interrupts();
    
    message_t* msg = &messages[message_id];
    
    // Find a free message slot for reply
    uint32_t reply_id = next_message_id;
    uint32_t start_id = reply_id;
    
    do {
        if (messages[reply_id].type == MSG_NONE)
            break;
        
        reply_id = (reply_id + 1) % MAX_MESSAGES;
        if (reply_id == start_id) {
            cpu_enable_interrupts();
            return; // No free slots for reply
        }
    } while (1);
    
    // Create reply message
    messages[reply_id].type = msg->type;
    messages[reply_id].sender = current_task_id;
    messages[reply_id].receiver = msg->sender;
    messages[reply_id].size = reply_size;
    messages[reply_id].is_replied = true;
    messages[reply_id].reply_id = message_id;
    
    if (reply_data && reply_size > 0) {
        for (uint32_t i = 0; i < reply_size && i < MAX_MESSAGE_SIZE; i++) {
            messages[reply_id].data[i] = ((uint8_t*)reply_data)[i];
        }
    }
    
    // Update next message ID
    next_message_id = (reply_id + 1) % MAX_MESSAGES;
    
    // Mark original message as replied
    msg->is_replied = true;
    msg->reply_id = reply_id;
    
    // If sender is waiting for reply, unblock it
    if (tasks[msg->sender].state == TASK_BLOCKED) {
        tasks[msg->sender].pending_message = reply_id;
        tasks[msg->sender].state = TASK_READY;
    } else {
        tasks[msg->sender].pending_message = reply_id;
    }
    
    cpu_enable_interrupts();
}

/* System call handlers */
void syscall_handler(uint32_t syscall_number, void* params) {
    switch (syscall_number) {
        case 1: // create_task
            // Implementation here
            break;
            
        case 2: // send_message
            // Implementation here
            break;
            
        case 3: // receive_message
            // Implementation here
            break;
            
        case 4: // reply_message
            // Implementation here
            break;
            
        default:
            // Unknown syscall
            break;
    }
}

/* Interrupt handlers */
void timer_interrupt_handler(void) {
    // Acknowledge interrupt
    
    // Perform task scheduling
    task_schedule();
}

/* Sample user tasks */
void idle_task(void) {
    while (1) {
        // Just yield CPU
        task_schedule();
    }
}

void echo_server_task(void) {
    while (1) {
        uint32_t sender;
        message_type_t type;
        uint8_t data[MAX_MESSAGE_SIZE];
        uint32_t size;
        
        // Wait for incoming message
        if (receive_message(&sender, &type, data, &size)) {
            // Echo back with same data
            reply_message(tasks[current_task_id].pending_message, data, size);
        }
    }
}

/* Kernel main - entry point after bootstrap */
void kernel_main(void) {
    // Initialize system
    next_message_id = 0;
    
    // Create initial system tasks
    task_create(idle_task);
    task_create(echo_server_task);
    
    // Enable interrupts
    cpu_enable_interrupts();
    
    // Enter the scheduler loop
    while (1) {
        task_schedule();
    }
}
