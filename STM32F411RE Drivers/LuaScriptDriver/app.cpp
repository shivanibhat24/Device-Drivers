/**
 * app.cpp — Lua 5.4 Embedded Scripting Engine
 * Target : STM32 Nucleo F411RE (512KB Flash, 128KB RAM)
 * RTOS   : FreeRTOS (heap_4)
 * Toolchain: arm-none-eabi-g++ via STM32CubeIDE
 *
 * Architecture
 * ─────────────
 *  main.c  (CubeMX-generated, untouched)
 *    └── calls cpp_main() via extern "C"
 *
 *  app.cpp (this file)
 *    ├── LuaEngine   — wraps lua_State, custom allocator, API bindings
 *    ├── ScriptStore — const scripts stored in Flash
 *    ├── lua_task()  — FreeRTOS task: reads commands from UART queue, runs scripts
 *    └── uart_task() — FreeRTOS task: receives UART bytes, assembles commands
 *
 * UART Protocol (115200 8N1)
 * ─────────────────────────
 *  Send a Lua script terminated by '\n' on a single line for one-liners, OR
 *  wrap multi-line scripts between "---BEGIN---\n" ... "---END---\n"
 *
 *  Built-in Lua globals exposed:
 *    gpio.set(pin, val)       -- set GPIO pin HIGH(1) or LOW(0)
 *    gpio.get(pin)            -- read GPIO pin, returns 0 or 1
 *    uart.print(str)          -- send string over UART
 *    delay(ms)                -- FreeRTOS vTaskDelay wrapper
 *    adc.read(channel)        -- read ADC1 channel (0–15), returns raw 12-bit value
 *    led.on()  / led.off()    -- convenience: LD2 (PA5) on Nucleo
 *    uptime()                 -- ms since boot (xTaskGetTickCount * portTICK_PERIOD_MS)
 *
 * Memory Budget (tight on 128KB RAM — every byte counts)
 * ────────────────────────────────────────────────────────
 *  lua_task stack : 6 KB  (configMINIMAL_STACK_SIZE * 6 — see note below)
 *  uart_task stack: 512 B
 *  Lua heap pool  : 32 KB (static pool, NO malloc from system heap)
 *  FreeRTOS heap  : remaining (~60 KB after stacks + globals)
 *
 *  ** IMPORTANT: In FreeRTOSConfig.h set configTOTAL_HEAP_SIZE to at least 48*1024
 *
 * How to add to your CubeIDE project
 * ────────────────────────────────────
 *  1. Add Lua 5.4 sources to Core/Src/lua/ (all .c files, exclude lua.c & luac.c)
 *  2. Add Core/Src/lua/ to include paths in Project Properties
 *  3. Add this file to Core/Src/
 *  4. In main.c, inside the USER CODE BEGIN Includes section:
 *       extern void cpp_main(void);
 *  5. In main.c, after all peripheral init and before osKernelStart() or the
 *     infinite loop, call:
 *       cpp_main();
 *  6. In Project Properties → C/C++ Build → Settings → MCU GCC Compiler → Defines
 *     add: LUA_32BITS  (makes lua_Integer = int32_t, saves RAM on 32-bit MCU)
 *
 * Lua source files to EXCLUDE from build (not needed for embedding):
 *   lua.c, luac.c, ltests.c
 */

extern "C" {
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Lua 5.4 C headers */
#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
}

#include <cstring>
#include <cstdio>
#include <cstdint>

/* ─────────────────────────────────────────────────────────
   Hardware pin definitions (Nucleo F411RE defaults)
   ─────────────────────────────────────────────────────────*/
static constexpr uint16_t LED_PIN  = GPIO_PIN_5;
static GPIO_TypeDef* const LED_PORT = GPIOA;

/* Map Lua pin numbers 0–15 to real GPIO.
   Extend this table for your own board wiring. */
struct PinMap { GPIO_TypeDef* port; uint16_t pin; };
static const PinMap PIN_TABLE[] = {
    { GPIOA, GPIO_PIN_0  },  /* pin 0  — A0 on Nucleo */
    { GPIOA, GPIO_PIN_1  },  /* pin 1  — A1 */
    { GPIOA, GPIO_PIN_4  },  /* pin 2  — A2 */
    { GPIOB, GPIO_PIN_0  },  /* pin 3  — A3 */
    { GPIOC, GPIO_PIN_1  },  /* pin 4  — A4 */
    { GPIOC, GPIO_PIN_0  },  /* pin 5  — A5 */
    { GPIOA, GPIO_PIN_5  },  /* pin 6  — LD2 (LED) */
    { GPIOA, GPIO_PIN_9  },  /* pin 7  — D8 (check your board) */
};
static constexpr size_t PIN_TABLE_SIZE = sizeof(PIN_TABLE) / sizeof(PIN_TABLE[0]);

/* ─────────────────────────────────────────────────────────
   UART handle — must match what CubeMX generates for you.
   Change huart2 to whichever UART you configured.
   ─────────────────────────────────────────────────────────*/
extern UART_HandleTypeDef huart2;  /* Virtual COM port on Nucleo */
extern ADC_HandleTypeDef  hadc1;

/* ─────────────────────────────────────────────────────────
   Lua custom allocator — uses a static pool so Lua never
   touches malloc()/free() and never fragments the heap.
   ─────────────────────────────────────────────────────────*/
namespace {

constexpr size_t LUA_POOL_SIZE = 32 * 1024;  /* 32 KB dedicated to Lua */
static uint8_t lua_pool[LUA_POOL_SIZE] __attribute__((aligned(8)));

struct Block {
    Block*  next;
    size_t  size;  /* usable bytes after this header */
};

/* Very simple first-fit allocator over the static pool.
   Good enough for Lua's allocation pattern (not general-purpose). */
static Block* pool_head = nullptr;

static void pool_init() {
    pool_head = reinterpret_cast<Block*>(lua_pool);
    pool_head->next = nullptr;
    pool_head->size = LUA_POOL_SIZE - sizeof(Block);
}

static void* pool_alloc(size_t size) {
    /* Align to 8 bytes */
    size = (size + 7u) & ~7u;
    Block* prev = nullptr;
    Block* cur  = pool_head;
    while (cur) {
        if (cur->size >= size) {
            size_t remaining = cur->size - size;
            if (remaining >= sizeof(Block) + 8) {
                /* Split block */
                Block* split = reinterpret_cast<Block*>(
                    reinterpret_cast<uint8_t*>(cur) + sizeof(Block) + size);
                split->size = remaining - sizeof(Block);
                split->next = cur->next;
                cur->next   = split;
                cur->size   = size;
            }
            /* Remove from free list */
            if (prev) prev->next = cur->next;
            else       pool_head = cur->next;
            return reinterpret_cast<uint8_t*>(cur) + sizeof(Block);
        }
        prev = cur;
        cur  = cur->next;
    }
    return nullptr;  /* Out of pool memory */
}

static void pool_free(void* ptr) {
    if (!ptr) return;
    Block* b = reinterpret_cast<Block*>(
        reinterpret_cast<uint8_t*>(ptr) - sizeof(Block));
    /* Insert back into free list (sorted by address for coalescing) */
    Block* prev = nullptr;
    Block* cur  = pool_head;
    while (cur && cur < b) { prev = cur; cur = cur->next; }
    b->next = cur;
    if (prev) prev->next = b; else pool_head = b;

    /* Coalesce forward */
    if (b->next &&
        reinterpret_cast<uint8_t*>(b) + sizeof(Block) + b->size ==
        reinterpret_cast<uint8_t*>(b->next)) {
        b->size += sizeof(Block) + b->next->size;
        b->next  = b->next->next;
    }
    /* Coalesce backward */
    if (prev &&
        reinterpret_cast<uint8_t*>(prev) + sizeof(Block) + prev->size ==
        reinterpret_cast<uint8_t*>(b)) {
        prev->size += sizeof(Block) + b->size;
        prev->next  = b->next;
    }
}

/* Lua allocator callback */
static void* lua_allocator(void* /*ud*/, void* ptr, size_t /*osize*/, size_t nsize) {
    if (nsize == 0) { pool_free(ptr); return nullptr; }
    if (ptr == nullptr) return pool_alloc(nsize);
    /* Realloc: alloc new, copy, free old */
    void* n = pool_alloc(nsize);
    if (n && ptr) {
        Block* old = reinterpret_cast<Block*>(
            reinterpret_cast<uint8_t*>(ptr) - sizeof(Block));
        std::memcpy(n, ptr, old->size < nsize ? old->size : nsize);
        pool_free(ptr);
    }
    return n;
}

} /* anonymous namespace */

/* ─────────────────────────────────────────────────────────
   UART utility — thread-safe write using a mutex
   ─────────────────────────────────────────────────────────*/
static SemaphoreHandle_t uart_mutex = nullptr;

static void uart_send(const char* str) {
    if (!str) return;
    if (uart_mutex) xSemaphoreTake(uart_mutex, portMAX_DELAY);
    HAL_UART_Transmit(&huart2,
                      reinterpret_cast<const uint8_t*>(str),
                      static_cast<uint16_t>(std::strlen(str)),
                      100);
    if (uart_mutex) xSemaphoreGive(uart_mutex);
}

static void uart_sendf(const char* fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    uart_send(buf);
}

/* ─────────────────────────────────────────────────────────
   Lua C API bindings — each function is a lua_CFunction
   ─────────────────────────────────────────────────────────*/

/* gpio.set(pin, value) */
static int l_gpio_set(lua_State* L) {
    int pin = static_cast<int>(luaL_checkinteger(L, 1));
    int val = static_cast<int>(luaL_checkinteger(L, 2));
    if (pin < 0 || static_cast<size_t>(pin) >= PIN_TABLE_SIZE)
        return luaL_error(L, "gpio.set: pin %d out of range (0–%d)", pin, PIN_TABLE_SIZE - 1);
    HAL_GPIO_WritePin(PIN_TABLE[pin].port,
                      PIN_TABLE[pin].pin,
                      val ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return 0;
}

/* gpio.get(pin) → integer */
static int l_gpio_get(lua_State* L) {
    int pin = static_cast<int>(luaL_checkinteger(L, 1));
    if (pin < 0 || static_cast<size_t>(pin) >= PIN_TABLE_SIZE)
        return luaL_error(L, "gpio.get: pin %d out of range", pin);
    GPIO_PinState s = HAL_GPIO_ReadPin(PIN_TABLE[pin].port, PIN_TABLE[pin].pin);
    lua_pushinteger(L, s == GPIO_PIN_SET ? 1 : 0);
    return 1;
}

/* uart.print(str) */
static int l_uart_print(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    uart_send(s);
    return 0;
}

/* delay(ms) — yields to FreeRTOS scheduler */
static int l_delay(lua_State* L) {
    lua_Integer ms = luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    return 0;
}

/* adc.read(channel) → integer (12-bit raw) */
static int l_adc_read(lua_State* L) {
    lua_Integer ch = luaL_checkinteger(L, 1);
    if (ch < 0 || ch > 15)
        return luaL_error(L, "adc.read: channel must be 0–15");

    /* Configure ADC channel on the fly */
    ADC_ChannelConfTypeDef cfg = {};
    cfg.Channel      = static_cast<uint32_t>(ch);
    cfg.Rank         = 1;
    cfg.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &cfg);

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint32_t raw = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    lua_pushinteger(L, static_cast<lua_Integer>(raw));
    return 1;
}

/* led.on() */
static int l_led_on(lua_State* /*L*/) {
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
    return 0;
}

/* led.off() */
static int l_led_off(lua_State* /*L*/) {
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    return 0;
}

/* uptime() → integer (milliseconds) */
static int l_uptime(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(
        xTaskGetTickCount() * portTICK_PERIOD_MS));
    return 1;
}

/* ─────────────────────────────────────────────────────────
   LuaEngine — owns the lua_State, registers all bindings
   ─────────────────────────────────────────────────────────*/
class LuaEngine {
public:
    LuaEngine() : L_(nullptr) {}
    ~LuaEngine() { if (L_) lua_close(L_); }

    /* Returns true on success */
    bool init() {
        pool_init();
        L_ = lua_newstate(lua_allocator, nullptr);
        if (!L_) { uart_send("[LUA] lua_newstate failed — pool too small?\r\n"); return false; }

        /* Open only safe standard libs (no io, no os, no package — no filesystem) */
        luaL_requiref(L_, "_G",       luaopen_base,   1); lua_pop(L_, 1);
        luaL_requiref(L_, "string",   luaopen_string, 1); lua_pop(L_, 1);
        luaL_requiref(L_, "table",    luaopen_table,  1); lua_pop(L_, 1);
        luaL_requiref(L_, "math",     luaopen_math,   1); lua_pop(L_, 1);

        register_gpio();
        register_uart();
        register_adc();
        register_led();

        /* Global: delay, uptime */
        lua_pushcfunction(L_, l_delay);  lua_setglobal(L_, "delay");
        lua_pushcfunction(L_, l_uptime); lua_setglobal(L_, "uptime");

        uart_send("[LUA] Engine ready. Send Lua script over UART.\r\n");
        return true;
    }

    /* Run a null-terminated Lua script string. Reports errors over UART. */
    void run(const char* script) {
        if (!L_) return;
        int err = luaL_dostring(L_, script);
        if (err != LUA_OK) {
            const char* msg = lua_tostring(L_, -1);
            uart_sendf("[LUA ERROR] %s\r\n", msg ? msg : "(unknown)");
            lua_pop(L_, 1);
        }
    }

private:
    lua_State* L_;

    void register_gpio() {
        lua_newtable(L_);
        lua_pushcfunction(L_, l_gpio_set); lua_setfield(L_, -2, "set");
        lua_pushcfunction(L_, l_gpio_get); lua_setfield(L_, -2, "get");
        lua_setglobal(L_, "gpio");
    }

    void register_uart() {
        lua_newtable(L_);
        lua_pushcfunction(L_, l_uart_print); lua_setfield(L_, -2, "print");
        lua_setglobal(L_, "uart");
    }

    void register_adc() {
        lua_newtable(L_);
        lua_pushcfunction(L_, l_adc_read); lua_setfield(L_, -2, "read");
        lua_setglobal(L_, "adc");
    }

    void register_led() {
        lua_newtable(L_);
        lua_pushcfunction(L_, l_led_on);  lua_setfield(L_, -2, "on");
        lua_pushcfunction(L_, l_led_off); lua_setfield(L_, -2, "off");
        lua_setglobal(L_, "led");
    }
};

/* ─────────────────────────────────────────────────────────
   ScriptStore — pre-loaded demo scripts in Flash (read-only)
   Reference these by name at the UART prompt:
     run:blink
     run:adc_poll
   ─────────────────────────────────────────────────────────*/
struct Script { const char* name; const char* code; };

static const Script FLASH_SCRIPTS[] = {
    {
        "blink",
        "uart.print('Blinking LED 10 times...\\n')\n"
        "for i = 1, 10 do\n"
        "  led.on()\n"
        "  delay(200)\n"
        "  led.off()\n"
        "  delay(200)\n"
        "end\n"
        "uart.print('Done.\\n')\n"
    },
    {
        "adc_poll",
        "uart.print('ADC poll — channel 0, 5 samples:\\n')\n"
        "for i = 1, 5 do\n"
        "  local v = adc.read(0)\n"
        "  local mv = math.floor(v * 3300 / 4095)\n"
        "  uart.print(string.format('  sample %d: %d raw (%d mV)\\n', i, v, mv))\n"
        "  delay(200)\n"
        "end\n"
    },
    {
        "uptime_check",
        "uart.print(string.format('Uptime: %d ms\\n', uptime()))\n"
    },
    {
        "gpio_toggle",
        "uart.print('Toggling pin 0 five times\\n')\n"
        "for i = 1, 5 do\n"
        "  gpio.set(0, 1)\n"
        "  delay(100)\n"
        "  gpio.set(0, 0)\n"
        "  delay(100)\n"
        "end\n"
    },
};
static constexpr size_t FLASH_SCRIPTS_COUNT =
    sizeof(FLASH_SCRIPTS) / sizeof(FLASH_SCRIPTS[0]);

static const char* find_flash_script(const char* name) {
    for (size_t i = 0; i < FLASH_SCRIPTS_COUNT; ++i)
        if (std::strcmp(FLASH_SCRIPTS[i].name, name) == 0)
            return FLASH_SCRIPTS[i].code;
    return nullptr;
}

/* ─────────────────────────────────────────────────────────
   UART receive — interrupt-driven byte accumulation.
   The ISR feeds bytes into this queue; uart_task assembles
   complete commands and posts to script_queue.
   ─────────────────────────────────────────────────────────*/
static QueueHandle_t byte_queue   = nullptr;
static QueueHandle_t script_queue = nullptr;

/* Called from HAL_UART_RxCpltCallback (see bottom of file).
   Sends one received byte into the queue from ISR context. */
static uint8_t rx_byte = 0;  /* DMA/interrupt receive buffer */

/* ─────────────────────────────────────────────────────────
   Script buffer — assembled by uart_task.
   Max script size: 512 bytes. Enough for meaningful programs.
   ─────────────────────────────────────────────────────────*/
constexpr size_t MAX_SCRIPT_LEN  = 512;
constexpr size_t MAX_CMD_LEN     = 64;

/* A script_queue message is a heap_4 allocated char* to avoid
   large stack frames. The lua_task frees it after use. */

/* ─────────────────────────────────────────────────────────
   FreeRTOS Tasks
   ─────────────────────────────────────────────────────────*/

/* uart_task — assembles incoming bytes into commands/scripts */
static void uart_task(void* /*arg*/) {
    static char buf[MAX_SCRIPT_LEN];
    size_t pos          = 0;
    bool   multi_line   = false;

    uart_send("\r\n[UART] Ready. Send Lua one-liners or ---BEGIN---/---END--- blocks.\r\n");
    uart_send("[UART] Preloaded scripts: blink, adc_poll, uptime_check, gpio_toggle\r\n");
    uart_send("[UART] Run with: run:blink\r\n\r\n");

    /* Start first receive */
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);

    while (true) {
        uint8_t b;
        if (xQueueReceive(byte_queue, &b, portMAX_DELAY) != pdTRUE) continue;

        /* Echo the byte back so terminal feels responsive */
        HAL_UART_Transmit(&huart2, &b, 1, 10);

        if (b == '\r') continue;  /* ignore CR in CRLF */

        if (pos < MAX_SCRIPT_LEN - 1) {
            buf[pos++] = static_cast<char>(b);
        } else {
            uart_send("\r\n[UART] Buffer overflow — clearing.\r\n");
            pos = 0; multi_line = false;
            continue;
        }

        buf[pos] = '\0';

        /* Detect multi-line begin marker */
        if (!multi_line && std::strstr(buf, "---BEGIN---\n")) {
            multi_line = true;
            /* Clear everything up to and including the marker */
            pos = 0; buf[0] = '\0';
            uart_send("[UART] Multi-line mode. Send script then ---END---\r\n");
            continue;
        }

        /* Detect end marker */
        if (multi_line && std::strstr(buf, "---END---\n")) {
            /* Strip the end marker */
            char* end = std::strstr(buf, "---END---");
            if (end) *end = '\0';
            /* Post to lua_task */
            char* script = static_cast<char*>(pvPortMalloc(pos + 1));
            if (script) {
                std::memcpy(script, buf, pos + 1);
                xQueueSend(script_queue, &script, 0);
            }
            pos = 0; buf[0] = '\0'; multi_line = false;
            continue;
        }

        /* Single-line command on newline */
        if (!multi_line && b == '\n') {
            /* Trim trailing whitespace */
            while (pos > 0 && (buf[pos-1] == '\n' || buf[pos-1] == '\r' || buf[pos-1] == ' '))
                buf[--pos] = '\0';

            if (pos == 0) continue;

            char* script = static_cast<char*>(pvPortMalloc(pos + 1));
            if (script) {
                std::memcpy(script, buf, pos + 1);
                xQueueSend(script_queue, &script, 0);
            }
            pos = 0; buf[0] = '\0';
        }
    }
}

/* lua_task — pops commands from script_queue, dispatches to LuaEngine */
static void lua_task(void* /*arg*/) {
    LuaEngine engine;
    if (!engine.init()) {
        uart_send("[LUA] Fatal: engine init failed.\r\n");
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        char* script = nullptr;
        if (xQueueReceive(script_queue, &script, portMAX_DELAY) != pdTRUE) continue;
        if (!script) continue;

        /* Check for "run:<name>" command to execute a Flash script */
        if (std::strncmp(script, "run:", 4) == 0) {
            const char* name = script + 4;
            const char* code = find_flash_script(name);
            if (code) {
                uart_sendf("[LUA] Running flash script: %s\r\n", name);
                engine.run(code);
            } else {
                uart_sendf("[LUA] Unknown script: '%s'\r\n", name);
                uart_send("[LUA] Available: blink, adc_poll, uptime_check, gpio_toggle\r\n");
            }
        } else {
            /* Treat as inline Lua */
            uart_send("[LUA] Executing inline script...\r\n");
            engine.run(script);
            uart_send("[LUA] Done.\r\n");
        }

        vPortFree(script);
    }
}

/* ─────────────────────────────────────────────────────────
   HAL callback — must be extern "C" so C linker can find it.
   Feeds received byte into byte_queue and re-arms interrupt.
   ─────────────────────────────────────────────────────────*/
extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
    if (huart->Instance != huart2.Instance) return;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(byte_queue, &rx_byte, &woken);
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);  /* re-arm */
    portYIELD_FROM_ISR(woken);
}

/* ─────────────────────────────────────────────────────────
   cpp_main — entry point called from main.c
   Creates queues, spawns tasks, returns immediately.
   The FreeRTOS scheduler (started in main.c) does the rest.
   ─────────────────────────────────────────────────────────*/
extern "C" void cpp_main(void) {
    /* Queues */
    byte_queue   = xQueueCreate(64,  sizeof(uint8_t));   /* raw bytes from ISR  */
    script_queue = xQueueCreate(4,   sizeof(char*));     /* script pointers     */
    uart_mutex   = xSemaphoreCreateMutex();

    configASSERT(byte_queue);
    configASSERT(script_queue);
    configASSERT(uart_mutex);

    /* Tasks
       lua_task needs a larger stack because Lua's VM uses it heavily.
       6144 bytes = 1536 words — adjust down if you run out of heap.
       uart_task is very lightweight. */
    xTaskCreate(lua_task,  "lua",  1536, nullptr, 2, nullptr);
    xTaskCreate(uart_task, "uart", 256,  nullptr, 3, nullptr);
    /* uart_task has higher priority so it never drops bytes while
       lua_task is busy executing a script. */
}

/*
 * ═══════════════════════════════════════════════════════════
 *  USAGE EXAMPLES (send these over your serial terminal)
 * ═══════════════════════════════════════════════════════════
 *
 *  1. Blink LD2 using a pre-loaded Flash script:
 *       run:blink
 *
 *  2. Read ADC five times:
 *       run:adc_poll
 *
 *  3. Inline one-liner:
 *       uart.print("Hello from Lua! Uptime: " .. uptime() .. "ms\n")
 *
 *  4. Multi-line script:
 *       ---BEGIN---
 *       for i = 1, 3 do
 *         led.on()
 *         delay(500)
 *         led.off()
 *         delay(500)
 *         uart.print("Blink " .. i .. "\n")
 *       end
 *       ---END---
 *
 *  5. Math in Lua:
 *       uart.print(string.format("sin(45) = %.4f\n", math.sin(math.pi/4)))
 *
 * ═══════════════════════════════════════════════════════════
 *  EXTENDING — adding your own Lua function
 * ═══════════════════════════════════════════════════════════
 *
 *  Step 1: Write the C function
 *    static int l_pwm_set(lua_State* L) {
 *        lua_Integer duty = luaL_checkinteger(L, 1);
 *        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);
 *        return 0;
 *    }
 *
 *  Step 2: Register it inside LuaEngine::init() or a new register_xxx():
 *    lua_newtable(L_);
 *    lua_pushcfunction(L_, l_pwm_set); lua_setfield(L_, -2, "set");
 *    lua_setglobal(L_, "pwm");
 *
 *  Now from Lua: pwm.set(512)
 */
