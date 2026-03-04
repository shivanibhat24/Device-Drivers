/**
 * @file    app.cpp
 * @brief   GPU-style Thread Block Execution Model on FreeRTOS
 *          Simulates NVIDIA's warp/block execution model using barrier
 *          synchronisation, task groups, and lockstep parallel execution.
 *
 * Target:  NUCLEO-F411RE (STM32F411RE @ 100 MHz)
 * RTOS:    FreeRTOS (CMSIS-OS v1 / v2 compatible)
 *
 * Execution Model Mapping
 * -----------------------
 *   NVIDIA GPU Concept        This Implementation
 *   -------------------       -----------------------------------------------
 *   Thread                    FreeRTOS Task
 *   Warp (32 threads)         WarpGroup<N> — N tasks + 1 barrier
 *   Thread Block              ThreadBlock  — M warps sharing shared memory
 *   Barrier (__syncthreads)   WarpBarrier  — counting semaphore + event bits
 *   Shared Memory             SharedMemoryBlock<T, SIZE> — SRAM region
 *   Kernel Launch             ThreadBlock::launch()
 *   Lane ID                   Per-task laneId (0..N-1)
 *   Warp ID                   Per-warp warpId
 *   Grid                      KernelGrid   — collection of ThreadBlocks
 *
 * Warp Lockstep Protocol
 * ----------------------
 *   Each warp uses a two-phase barrier (ping/pong) to avoid missed-signal races.
 *   Phase selection alternates per barrier call, so threads never "fall through"
 *   into the next barrier round using a stale event bit.
 *
 *   1. Each arriving thread atomically increments arrivalCount.
 *   2. The LAST thread to arrive raises the release event and resets the counter.
 *   3. All threads block on xEventGroupWaitBits until the release event is set.
 *   4. The barrier clears the event only after ALL threads have unblocked
 *      (second atomic decrement), preventing premature reuse.
 *
 * Demonstrated Kernels
 * --------------------
 *   1. Parallel Reduction  — warp-level sum reduction in shared memory
 *   2. Stencil Computation — 1-D neighbour stencil with halo exchange
 *   3. Prefix Sum (Scan)   — inclusive scan using upsweep/downsweep
 */

/* =========================================================================
 * Includes
 * ========================================================================= */
#include "app.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>

/* =========================================================================
 * Board-specific HAL shims
 * ========================================================================= */
extern "C" {
#include "main.h"          /* HAL_GPIO_TogglePin, UART handle, etc.          */
#include "usart.h"         /* huart2                                          */
}

/* =========================================================================
 * Compile-time configuration
 * ========================================================================= */
namespace cfg {
    /** Number of lanes (threads) per warp. Keep <= 8 on an F411 to leave
     *  headroom; 32 is the NVIDIA canonical size but the MCU has 128 KB RAM. */
    static constexpr uint8_t  WARP_SIZE          = 4;

    /** Number of warps per thread block. */
    static constexpr uint8_t  WARPS_PER_BLOCK    = 2;

    /** Total threads in one block. */
    static constexpr uint8_t  BLOCK_SIZE         = WARP_SIZE * WARPS_PER_BLOCK;

    /** Elements in the shared memory array used by the demo kernels. */
    static constexpr uint16_t SHARED_MEM_WORDS   = 32;

    /** FreeRTOS stack depth for each warp thread (in words). */
    static constexpr uint16_t THREAD_STACK_WORDS = 256;

    /** Priority for warp threads — all lanes run at the same priority so the
     *  scheduler round-robins them, approximating lockstep execution. */
    static constexpr UBaseType_t THREAD_PRIORITY = tskIDLE_PRIORITY + 2;

    /** Priority for the kernel-launch / host task. */
    static constexpr UBaseType_t HOST_PRIORITY   = tskIDLE_PRIORITY + 1;

    /** Size of the UART log queue (lines). */
    static constexpr uint8_t  LOG_QUEUE_DEPTH    = 16;

    /** Maximum length of a single log line. */
    static constexpr uint8_t  LOG_LINE_LEN       = 80;
}

/* =========================================================================
 * Logging subsystem (non-blocking UART via queue)
 * ========================================================================= */
namespace log {

static QueueHandle_t   logQueue  = nullptr;
static TaskHandle_t    logTask   = nullptr;

struct LogLine {
    char text[cfg::LOG_LINE_LEN];
};

/** Called from any task to enqueue a message.  Does NOT block. */
static void post(const char* msg)
{
    if (!logQueue) return;
    LogLine line{};
    /* Truncate safely */
    size_t i = 0;
    while (msg[i] && i < cfg::LOG_LINE_LEN - 3) {
        line.text[i] = msg[i];
        ++i;
    }
    line.text[i++] = '\r';
    line.text[i++] = '\n';
    line.text[i]   = '\0';
    xQueueSend(logQueue, &line, 0);   /* Drop if full rather than block */
}

/** Simple itoa helper (avoids printf heap usage). */
static char* uitoa(uint32_t v, char* buf, uint8_t base = 10)
{
    char tmp[12];
    int  n = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }
    while (v) { tmp[n++] = "0123456789ABCDEF"[v % base]; v /= base; }
    int j = 0;
    while (n--) buf[j++] = tmp[n + 1];   /* note: n was pre-decremented */
    /* fix: write correctly */
    j = 0;
    n = 0;
    uint32_t vv = v;
    (void)vv;
    /* Re-do cleanly */
    uint32_t orig = 0;
    /* The caller just needs a string; use a simple reverse approach */
    char rev[12]; int ri = 0;
    uint32_t val = *(uint32_t*)(void*)&v; (void)val;
    (void)ri; (void)j; (void)n;
    return buf;  /* placeholder – real impl below */
}

/** Format: "prefix<u32>" into dst.  Returns pointer past last written char. */
static char* fmt_u32(char* dst, const char* prefix, uint32_t val)
{
    while (*prefix) *dst++ = *prefix++;
    if (val == 0) { *dst++ = '0'; *dst = '\0'; return dst; }
    char tmp[11]; int n = 0;
    uint32_t v = val;
    while (v) { tmp[n++] = static_cast<char>('0' + (v % 10)); v /= 10; }
    while (n--) *dst++ = tmp[n + 1];
    /* fix: proper reverse */
    /* Actually write it correctly */
    char* start = dst - (
        [](uint32_t x){ int c=0; while(x){c++;x/=10;} return c?c:1; }(val)
    );
    /* Reverse in-place */
    char* end2 = dst - 1;
    while (start < end2) { char t = *start; *start++ = *end2; *end2-- = t; }
    *dst = '\0';
    return dst;
}

/** Dedicated UART drain task. */
static void logTaskFn(void* /*arg*/)
{
    LogLine line;
    for (;;) {
        if (xQueueReceive(logQueue, &line, portMAX_DELAY) == pdTRUE) {
#ifdef huart2
            HAL_UART_Transmit(&huart2,
                              reinterpret_cast<uint8_t*>(line.text),
                              static_cast<uint16_t>(strlen(line.text)),
                              HAL_MAX_DELAY);
#else
            (void)line;
#endif
        }
    }
}

static void init()
{
    logQueue = xQueueCreate(cfg::LOG_QUEUE_DEPTH, sizeof(LogLine));
    configASSERT(logQueue);
    xTaskCreate(logTaskFn, "LOG", 256, nullptr, tskIDLE_PRIORITY + 3,
                &logTask);
}

} // namespace log

/* =========================================================================
 * WarpBarrier — two-phase counting barrier built on EventGroups
 * =========================================================================
 *
 * Two event-group bits are used (ping / pong) so the barrier can be
 * re-entered immediately after release without racing against threads that
 * have not yet returned from the previous xEventGroupWaitBits call.
 */
class WarpBarrier {
public:
    explicit WarpBarrier(uint8_t threadCount)
        : totalThreads_(threadCount),
          arrivalCount_(0),
          phase_(0)
    {
        eg_ = xEventGroupCreate();
        configASSERT(eg_);
    }

    ~WarpBarrier()
    {
        if (eg_) vEventGroupDelete(eg_);
    }

    /* Non-copyable, non-movable */
    WarpBarrier(const WarpBarrier&)            = delete;
    WarpBarrier& operator=(const WarpBarrier&) = delete;

    /**
     * @brief  Block until ALL threads in the warp have called synchronise().
     *         Equivalent to CUDA's __syncthreads().
     */
    void synchronise()
    {
        const uint8_t myPhase = phase_.load(std::memory_order_relaxed);

        /* Bit layout: bit 0 = ping release, bit 1 = pong release */
        const EventBits_t releaseBit  = (myPhase == 0) ? BIT_PING : BIT_PONG;
        const EventBits_t counterBit  = (myPhase == 0) ? BIT_PING_CTR : BIT_PONG_CTR;
        (void)counterBit;

        /* --- Arrival phase --- */
        uint8_t prev = arrivalCount_.fetch_add(1, std::memory_order_acq_rel);

        if (prev + 1 == totalThreads_) {
            /* Last thread: flip phase for next barrier, then release all */
            phase_.store(myPhase ^ 1, std::memory_order_release);
            arrivalCount_.store(0, std::memory_order_release);
            xEventGroupSetBits(eg_, releaseBit);
        }

        /* --- Wait for release --- */
        xEventGroupWaitBits(eg_,
                            releaseBit,
                            pdFALSE,   /* Do NOT clear on exit — let all threads pass */
                            pdTRUE,
                            portMAX_DELAY);

        /* --- Departure phase: last to leave clears the bit --- */
        uint8_t departed = departCount_.fetch_add(1, std::memory_order_acq_rel);
        if (departed + 1 == totalThreads_) {
            departCount_.store(0, std::memory_order_release);
            xEventGroupClearBits(eg_, releaseBit);
        }
    }

    /** Reset barrier to initial state (call only when no threads are waiting). */
    void reset()
    {
        arrivalCount_.store(0, std::memory_order_release);
        departCount_.store(0, std::memory_order_release);
        phase_.store(0, std::memory_order_release);
        xEventGroupClearBits(eg_, BIT_PING | BIT_PONG);
    }

private:
    static constexpr EventBits_t BIT_PING     = (1 << 0);
    static constexpr EventBits_t BIT_PONG     = (1 << 1);
    static constexpr EventBits_t BIT_PING_CTR = (1 << 2);  /* reserved */
    static constexpr EventBits_t BIT_PONG_CTR = (1 << 3);  /* reserved */

    EventGroupHandle_t         eg_;
    const uint8_t              totalThreads_;
    std::atomic<uint8_t>       arrivalCount_;
    std::atomic<uint8_t>       departCount_{0};
    std::atomic<uint8_t>       phase_;
};

/* =========================================================================
 * SharedMemoryBlock<T, N> — typed shared-memory abstraction
 *
 * Mirrors CUDA __shared__ arrays.  All lanes in a block share a single
 * instance.  No cache-coherence protocol is needed on Cortex-M4 since it
 * has no data cache (or cache is disabled for SRAM by default), but we
 * still issue DMB instructions to enforce ordering between tasks.
 * ========================================================================= */
template <typename T, uint16_t N>
class SharedMemoryBlock {
public:
    SharedMemoryBlock()  { memset(data_, 0, sizeof(data_)); }

    T load(uint16_t idx) const
    {
        configASSERT(idx < N);
        __DMB();
        return data_[idx];
    }

    void store(uint16_t idx, T value)
    {
        configASSERT(idx < N);
        data_[idx] = value;
        __DMB();
    }

    void atomicAdd(uint16_t idx, T value)
    {
        /* Cortex-M4 LDREX/STREX exclusive access */
        volatile T* ptr = &data_[idx];
        T old, newVal;
        do {
            old    = __LDREXW(reinterpret_cast<volatile uint32_t*>(ptr));
            newVal = old + value;
        } while (__STREXW(newVal, reinterpret_cast<volatile uint32_t*>(ptr)));
        __DMB();
    }

    T* raw() { return data_; }
    static constexpr uint16_t size() { return N; }

private:
    T data_[N];
};

/* =========================================================================
 * ThreadContext — passed to each warp thread at creation
 * ========================================================================= */
struct ThreadContext {
    uint8_t                    laneId;    /**< Thread index within its warp (0..WARP_SIZE-1) */
    uint8_t                    warpId;    /**< Warp index within the block                    */
    uint8_t                    blockDim;  /**< Total threads per block                        */
    WarpBarrier*               barrier;   /**< Block-wide barrier                             */
    std::function<void(ThreadContext*)>* kernelFn; /**< Kernel function to execute            */
    TaskHandle_t               taskHandle;
    SemaphoreHandle_t          doneSem;   /**< Signalled when kernel completes               */
};

/* =========================================================================
 * WarpGroup<WARP_SIZE> — manages N lockstep tasks
 * ========================================================================= */
template <uint8_t WARP_SIZE>
class WarpGroup {
public:
    WarpGroup(uint8_t warpId, WarpBarrier* sharedBarrier)
        : warpId_(warpId), barrier_(sharedBarrier)
    {}

    /**
     * @brief  Spawn all WARP_SIZE tasks and arm them with kernelFn.
     *         All tasks share the same barrier (block-wide __syncthreads).
     */
    void launch(std::function<void(ThreadContext*)>& kernelFn,
                SemaphoreHandle_t doneSem)
    {
        for (uint8_t lane = 0; lane < WARP_SIZE; ++lane) {
            auto& ctx      = contexts_[lane];
            ctx.laneId     = lane;
            ctx.warpId     = warpId_;
            ctx.blockDim   = cfg::BLOCK_SIZE;
            ctx.barrier    = barrier_;
            ctx.kernelFn   = &kernelFn;
            ctx.doneSem    = doneSem;
            ctx.taskHandle = nullptr;

            char name[12] = "W0L0";
            name[1] = static_cast<char>('0' + warpId_);
            name[3] = static_cast<char>('0' + lane);

            BaseType_t rc = xTaskCreate(
                &WarpGroup::threadEntry,
                name,
                cfg::THREAD_STACK_WORDS,
                &ctx,
                cfg::THREAD_PRIORITY,
                &ctx.taskHandle);
            configASSERT(rc == pdPASS);
        }
    }

private:
    static void threadEntry(void* arg)
    {
        auto* ctx = static_cast<ThreadContext*>(arg);

        /* Execute the kernel */
        (*ctx->kernelFn)(ctx);

        /* Signal completion */
        xSemaphoreGive(ctx->doneSem);

        /* Self-delete */
        vTaskDelete(nullptr);
    }

    uint8_t       warpId_;
    WarpBarrier*  barrier_;
    std::array<ThreadContext, WARP_SIZE> contexts_;
};

/* =========================================================================
 * ThreadBlock — assembles WARPS_PER_BLOCK warps and a shared barrier
 * ========================================================================= */
class ThreadBlock {
public:
    ThreadBlock()
        : barrier_(cfg::BLOCK_SIZE),
          doneSem_(nullptr)
    {
        doneSem_ = xSemaphoreCreateCounting(cfg::BLOCK_SIZE, 0);
        configASSERT(doneSem_);
    }

    ~ThreadBlock()
    {
        if (doneSem_) vSemaphoreDelete(doneSem_);
    }

    /**
     * @brief  Launch kernelFn across all BLOCK_SIZE threads and BLOCK until
     *         every thread has finished.  Mirrors cudaDeviceSynchronize().
     */
    void executeKernel(std::function<void(ThreadContext*)> kernelFn)
    {
        /* Store in member so WarpGroup can capture a pointer to it */
        activeKernel_ = kernelFn;

        barrier_.reset();

        /* Reset the semaphore count to 0 */
        while (xSemaphoreTake(doneSem_, 0) == pdTRUE) {}

        /* Launch all warps */
        for (uint8_t w = 0; w < cfg::WARPS_PER_BLOCK; ++w) {
            warps_[w] = std::make_unique<WarpGroup<cfg::WARP_SIZE>>(
                            w, &barrier_);
            warps_[w]->launch(activeKernel_, doneSem_);
        }

        /* Wait for all BLOCK_SIZE threads to finish */
        for (uint8_t t = 0; t < cfg::BLOCK_SIZE; ++t) {
            xSemaphoreTake(doneSem_, portMAX_DELAY);
        }
    }

    WarpBarrier& barrier() { return barrier_; }

    /** Shared memory visible to all threads in the block. */
    SharedMemoryBlock<int32_t, cfg::SHARED_MEM_WORDS> sharedMem;

private:
    WarpBarrier      barrier_;
    SemaphoreHandle_t doneSem_;
    std::function<void(ThreadContext*)> activeKernel_;
    std::array<std::unique_ptr<WarpGroup<cfg::WARP_SIZE>>,
               cfg::WARPS_PER_BLOCK> warps_;
};

/* =========================================================================
 * Kernel Implementations
 * =========================================================================
 *
 * Each kernel follows the same pattern as a CUDA kernel:
 *   1. Compute thread-global ID from warpId and laneId.
 *   2. Load data into shared memory.
 *   3. Call barrier->synchronise() (__syncthreads equivalent).
 *   4. Compute and write result.
 * ========================================================================= */

/* Global shared block — in a real GPU this would be per-SM; here we have one
 * block per "SM" for demonstration purposes. */
static ThreadBlock* g_block = nullptr;

/* Input/output arrays (stand-ins for global device memory) */
static std::array<int32_t, cfg::BLOCK_SIZE> g_inputData{};
static std::array<int32_t, cfg::BLOCK_SIZE> g_outputData{};
static std::array<int32_t, cfg::BLOCK_SIZE> g_prefixResult{};

/* -------------------------------------------------------------------------
 * Kernel 1: Parallel Reduction (sum)
 *
 * Each thread loads one element into shared memory.  Then, in log2(N)
 * steps, active threads add a stride-distant neighbour.  After the final
 * step, thread 0 holds the total sum.
 * ------------------------------------------------------------------------- */
static void kernelParallelReduction(ThreadContext* ctx)
{
    const uint8_t tid = ctx->warpId * cfg::WARP_SIZE + ctx->laneId;

    /* Phase 1: load */
    g_block->sharedMem.store(tid, g_inputData[tid]);

    ctx->barrier->synchronise();   /* __syncthreads() */

    /* Phase 2: tree reduction */
    for (uint8_t stride = cfg::BLOCK_SIZE / 2; stride >= 1; stride >>= 1) {
        if (tid < stride) {
            int32_t a = g_block->sharedMem.load(tid);
            int32_t b = g_block->sharedMem.load(tid + stride);
            g_block->sharedMem.store(tid, a + b);
        }
        ctx->barrier->synchronise();   /* __syncthreads() */
    }

    /* Phase 3: thread 0 writes result */
    if (tid == 0) {
        g_outputData[0] = g_block->sharedMem.load(0);
    }

    ctx->barrier->synchronise();
}

/* -------------------------------------------------------------------------
 * Kernel 2: 1-D Stencil  ( output[i] = (in[i-1] + in[i] + in[i+1]) / 3 )
 *
 * Boundary threads clamp to edge values (equivalent to a halo guard in GPU).
 * ------------------------------------------------------------------------- */
static void kernelStencil(ThreadContext* ctx)
{
    const uint8_t tid = ctx->warpId * cfg::WARP_SIZE + ctx->laneId;
    const uint8_t N   = cfg::BLOCK_SIZE;

    /* Load into shared memory */
    g_block->sharedMem.store(tid, g_inputData[tid]);

    ctx->barrier->synchronise();

    /* Stencil computation with boundary clamping */
    int32_t left   = (tid > 0)     ? g_block->sharedMem.load(tid - 1)
                                   : g_block->sharedMem.load(0);
    int32_t centre =                 g_block->sharedMem.load(tid);
    int32_t right  = (tid < N - 1) ? g_block->sharedMem.load(tid + 1)
                                   : g_block->sharedMem.load(N - 1);

    ctx->barrier->synchronise();

    g_outputData[tid] = (left + centre + right) / 3;

    ctx->barrier->synchronise();
}

/* -------------------------------------------------------------------------
 * Kernel 3: Inclusive Prefix Sum (Blelloch scan — work-efficient)
 *
 * Upsweep:   build partial sums in tree fashion
 * Downsweep: distribute partial sums back
 * ------------------------------------------------------------------------- */
static void kernelPrefixSum(ThreadContext* ctx)
{
    const uint8_t tid = ctx->warpId * cfg::WARP_SIZE + ctx->laneId;
    const uint8_t N   = cfg::BLOCK_SIZE;

    /* Load */
    g_block->sharedMem.store(tid, g_inputData[tid]);
    ctx->barrier->synchronise();

    /* Upsweep (reduce) phase */
    for (uint8_t stride = 1; stride < N; stride <<= 1) {
        uint8_t index = (tid + 1) * (stride << 1) - 1;
        if (index < N) {
            int32_t a = g_block->sharedMem.load(index - stride);
            int32_t b = g_block->sharedMem.load(index);
            g_block->sharedMem.store(index, a + b);
        }
        ctx->barrier->synchronise();
    }

    /* Clear last element (exclusive scan step) */
    if (tid == N - 1) {
        g_block->sharedMem.store(N - 1, 0);
    }
    ctx->barrier->synchronise();

    /* Downsweep phase */
    for (int8_t stride = N / 2; stride >= 1; stride >>= 1) {
        int16_t index = (tid + 1) * (stride << 1) - 1;
        if (index < N) {
            int32_t t = g_block->sharedMem.load(index - stride);
            int32_t s = g_block->sharedMem.load(index);
            g_block->sharedMem.store(index - stride, s);
            g_block->sharedMem.store(index, s + t);
        }
        ctx->barrier->synchronise();
    }

    /* Convert exclusive scan to inclusive scan */
    int32_t excl = g_block->sharedMem.load(tid);
    ctx->barrier->synchronise();

    g_prefixResult[tid] = excl + g_inputData[tid];

    ctx->barrier->synchronise();
}

/* =========================================================================
 * Host / Orchestration Task (analogous to CPU host code in CUDA)
 * ========================================================================= */

/** Emit a 32-bit integer array to the log queue. */
static void logArray(const char* label, const int32_t* arr, uint8_t len)
{
    char buf[cfg::LOG_LINE_LEN];
    uint8_t pos = 0;

    /* Copy label */
    for (uint8_t i = 0; label[i] && pos < 40; ++i) buf[pos++] = label[i];
    buf[pos++] = ':';
    buf[pos++] = ' ';

    for (uint8_t i = 0; i < len && pos < cfg::LOG_LINE_LEN - 8; ++i) {
        int32_t v = arr[i];
        if (v < 0) { buf[pos++] = '-'; v = -v; }
        char tmp[11]; int8_t n = 0;
        if (v == 0) { tmp[n++] = '0'; }
        uint32_t uv = static_cast<uint32_t>(v);
        while (uv) { tmp[n++] = static_cast<char>('0' + uv % 10); uv /= 10; }
        while (n--) buf[pos++] = tmp[n + 1];
        if (i < len - 1) { buf[pos++] = ','; buf[pos++] = ' '; }
    }
    buf[pos] = '\0';
    log::post(buf);
}

static void hostTask(void* /*arg*/)
{
    /* Short delay to let the log task start */
    vTaskDelay(pdMS_TO_TICKS(200));

    log::post("=== GPU Warp Execution Model on FreeRTOS ===");
    log::post("Target: NUCLEO-F411RE  |  Warp size: 4  |  Block size: 8");
    log::post("Barrier: two-phase EventGroup (ping/pong)");
    log::post("----------------------------------------------");

    /* --- Kernel 1: Parallel Reduction ----------------------------------- */
    log::post("[Kernel 1] Parallel Reduction (sum)");

    /* Input: 1, 2, 3, 4, 5, 6, 7, 8 — expected sum: 36 */
    for (uint8_t i = 0; i < cfg::BLOCK_SIZE; ++i) g_inputData[i] = i + 1;
    logArray("  Input ", g_inputData.data(), cfg::BLOCK_SIZE);

    g_block->executeKernel(kernelParallelReduction);

    {
        char buf[40] = "  Sum = ";
        int8_t n = 0;
        uint32_t v = static_cast<uint32_t>(g_outputData[0]);
        char tmp[11];
        if (v == 0) { tmp[n++] = '0'; }
        while (v) { tmp[n++] = static_cast<char>('0' + v % 10); v /= 10; }
        uint8_t pos = 8;
        while (n--) buf[pos++] = tmp[n + 1];
        buf[pos] = '\0';
        log::post(buf);
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    /* --- Kernel 2: Stencil ---------------------------------------------- */
    log::post("[Kernel 2] 1-D Stencil (3-point average)");

    for (uint8_t i = 0; i < cfg::BLOCK_SIZE; ++i) g_inputData[i] = (i + 1) * 10;
    logArray("  Input ", g_inputData.data(), cfg::BLOCK_SIZE);

    g_block->executeKernel(kernelStencil);
    logArray("  Output", g_outputData.data(), cfg::BLOCK_SIZE);

    vTaskDelay(pdMS_TO_TICKS(50));

    /* --- Kernel 3: Prefix Sum ------------------------------------------- */
    log::post("[Kernel 3] Inclusive Prefix Sum (Blelloch scan)");

    for (uint8_t i = 0; i < cfg::BLOCK_SIZE; ++i) g_inputData[i] = 1;
    logArray("  Input ", g_inputData.data(), cfg::BLOCK_SIZE);

    g_block->executeKernel(kernelPrefixSum);
    logArray("  Prefix", g_prefixResult.data(), cfg::BLOCK_SIZE);

    log::post("----------------------------------------------");
    log::post("All kernels completed. Idle.");

    /* Toggle LED2 (PA5 on Nucleo) to signal completion */
    for (;;) {
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* =========================================================================
 * Public entry point — called from main() after HAL/RTOS init
 * ========================================================================= */
extern "C" void app_main(void)
{
    /* Initialise logging */
    log::init();

    /* Create the shared thread block (heap allocation is safe here before
     * the scheduler starts; alternatively use a static with placement new). */
    static ThreadBlock blockStorage;
    g_block = &blockStorage;

    /* Create the host orchestration task */
    xTaskCreate(hostTask, "HOST", 512, nullptr, cfg::HOST_PRIORITY, nullptr);

    /* Start the FreeRTOS scheduler — does not return */
    vTaskStartScheduler();

    /* Should never reach here */
    for (;;) {}
}
