/**
 * ╔═══════════════════════════════════════════════════════════╗
 * ║               HANDYCAM FIRMWARE  v1.0.0                  ║
 * ║    Natural Language Controlled Smart Edge Camera System   ║
 * ╚═══════════════════════════════════════════════════════════╝
 *
 * Target:  ESP32-CAM (AI-Thinker module)
 * IDE:     Arduino IDE 2.x  |  Board: esp32 by Espressif v3.x
 * Libs:    ArduinoJson 7.x, ESP32 camera driver (built-in)
 *
 * HTTP Endpoint: POST /execute  (JSON body — see protocol docs)
 *
 * Author:  Handycam Project
 * License: MIT
 */

#include "src/system/config.h"
#include "src/comms/wifi_server.h"
#include "src/core/camera_driver.h"
#include "src/system/state_machine.h"
#include "src/execution/command_parser.h"
#include "src/execution/scheduler.h"

// ─── Global state ────────────────────────────────────────────
HandycamState g_state = STATE_IDLE;
CommandPayload g_pending_cmd;
volatile bool  g_cmd_ready = false;

// ─── Setup ───────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println(F("\n╔═══════════════════════════════╗"));
    Serial.println(F("║   HANDYCAM BOOT  v1.0.0      ║"));
    Serial.println(F("╚═══════════════════════════════╝"));

    // 1. Camera
    if (!camera_init()) {
        Serial.println(F("[FATAL] Camera init failed — halting"));
        while (true) delay(1000);
    }
    Serial.println(F("[OK]   Camera initialised"));

    // 2. WiFi + HTTP server
    wifi_server_begin();
    Serial.printf("[OK]   HTTP server at http://%s/execute\n",
                  WiFi.localIP().toString().c_str());

    // 3. State machine
    state_machine_init();
    Serial.println(F("[OK]   State machine ready"));
    Serial.println(F("[IDLE] Waiting for commands…\n"));
}

// ─── Loop ────────────────────────────────────────────────────
void loop() {
    // Poll HTTP server for incoming requests
    wifi_server_handle();

    // Process a pending command
    if (g_cmd_ready) {
        g_cmd_ready = false;
        state_machine_run(g_pending_cmd);
    }

    delay(10);
}
