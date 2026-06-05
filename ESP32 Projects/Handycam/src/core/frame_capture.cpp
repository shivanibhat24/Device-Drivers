#include "frame_capture.h"
#include <Arduino.h>

camera_fb_t* capture_frame() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println(F("[FRAME] esp_camera_fb_get() returned null"));
        return nullptr;
    }
    if (fb->format != PIXFORMAT_JPEG) {
        // Should not happen with PIXFORMAT_JPEG config, but guard anyway
        Serial.println(F("[FRAME] Unexpected pixel format"));
        esp_camera_fb_return(fb);
        return nullptr;
    }
    return fb;
}
