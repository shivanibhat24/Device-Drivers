#pragma once
// ─── HANDYCAM — System Configuration ────────────────────────

// ---------- WiFi credentials (change before flashing) --------
#define WIFI_SSID     "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PASSWORD"

// ---------- HTTP server --------------------------------------
#define HTTP_PORT     80

// ---------- Camera pin mapping (AI-Thinker ESP32-CAM) --------
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

// ---------- Capture limits -----------------------------------
#define MAX_BURST_COUNT     50      // hard cap per command
#define MAX_DELAY_MS     60000UL    // max initial delay  (60 s)
#define MAX_INTERVAL_MS  10000UL    // max inter-shot gap (10 s)
#define JPEG_QUALITY        12      // 0=best, 63=worst

// ---------- Debug --------------------------------------------
#define DEBUG_VERBOSE  1            // 0 = silent, 1 = verbose
