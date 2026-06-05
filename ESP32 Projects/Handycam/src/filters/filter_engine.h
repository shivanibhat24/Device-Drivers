#pragma once
#include "esp_camera.h"
#include "../execution/command_parser.h"

/**
 * Apply a filter preset to a JPEG frame buffer in-place.
 * The fb->buf is decoded, processed, and re-encoded as JPEG.
 *
 * NOTE: For JPEG buffers, pixel-level filters require decode→process→encode.
 *       On ESP32-CAM with PSRAM this is feasible for small frame sizes.
 *       For SVGA/UXGA on stock 520 KB SRAM, set FRAMESIZE to VGA or QVGA.
 */
void filter_apply(camera_fb_t* fb, FilterPreset preset, int brightness_delta = 0);

const char* filter_name(FilterPreset p);
