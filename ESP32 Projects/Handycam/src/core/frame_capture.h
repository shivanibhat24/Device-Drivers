#pragma once
#include "esp_camera.h"

/**
 * Capture a single JPEG frame from the sensor.
 * Caller MUST call esp_camera_fb_return(fb) after use.
 * Returns nullptr on failure.
 */
camera_fb_t* capture_frame();
