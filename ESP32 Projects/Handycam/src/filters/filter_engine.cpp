/**
 * filter_engine.cpp
 *
 * All filters operate on raw RGB888 pixels obtained by decoding the JPEG
 * frame buffer.  The processed pixels are re-encoded to JPEG in place.
 *
 * esp_jpg_decode / fmt2rgb888 are part of the ESP32 camera library.
 *
 * Supported presets:
 *   FILTER_NONE          – pass-through
 *   FILTER_GRAYSCALE     – luminance desaturation
 *   FILTER_SEPIA         – classic sepia tone matrix
 *   FILTER_CINEMATIC     – contrast boost + warm grade + subtle vignette
 *   FILTER_VINTAGE       – sepia + reduced saturation + luminance noise
 *   FILTER_WARM          – shift R+G up, B down
 *   FILTER_COOL          – shift B up, R down
 *   FILTER_BRIGHTNESS    – uniform brightness offset (param: brightness_delta)
 */

#include "filter_engine.h"
#include <Arduino.h>
#include <cstring>
#include <cstdlib>

// ─── Utility macros ──────────────────────────────────────────
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

// ─── Per-pixel filter functions ──────────────────────────────

static void apply_grayscale(uint8_t* p) {
    uint8_t y = (uint8_t)(0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]);
    p[0] = p[1] = p[2] = y;
}

static void apply_sepia(uint8_t* p) {
    uint8_t r = p[0], g = p[1], b = p[2];
    p[0] = (uint8_t)CLAMP((int)(r * 0.393f + g * 0.769f + b * 0.189f), 0, 255);
    p[1] = (uint8_t)CLAMP((int)(r * 0.349f + g * 0.686f + b * 0.168f), 0, 255);
    p[2] = (uint8_t)CLAMP((int)(r * 0.272f + g * 0.534f + b * 0.131f), 0, 255);
}

static void apply_warm(uint8_t* p) {
    p[0] = (uint8_t)CLAMP((int)p[0] + 20,  0, 255); // R +
    p[1] = (uint8_t)CLAMP((int)p[1] + 8,   0, 255); // G slight +
    p[2] = (uint8_t)CLAMP((int)p[2] - 20,  0, 255); // B -
}

static void apply_cool(uint8_t* p) {
    p[0] = (uint8_t)CLAMP((int)p[0] - 20,  0, 255); // R -
    p[1] = (uint8_t)CLAMP((int)p[1] + 5,   0, 255); // G slight +
    p[2] = (uint8_t)CLAMP((int)p[2] + 20,  0, 255); // B +
}

static void apply_brightness(uint8_t* p, int delta) {
    p[0] = (uint8_t)CLAMP((int)p[0] + delta, 0, 255);
    p[1] = (uint8_t)CLAMP((int)p[1] + delta, 0, 255);
    p[2] = (uint8_t)CLAMP((int)p[2] + delta, 0, 255);
}

// Contrast stretch helper
static void apply_contrast(uint8_t* p, float factor) {
    for (int c = 0; c < 3; c++) {
        int v = (int)((p[c] - 128) * factor + 128);
        p[c] = (uint8_t)CLAMP(v, 0, 255);
    }
}

static void apply_cinematic(uint8_t* p) {
    apply_contrast(p, 1.25f);   // punch contrast
    apply_warm(p);               // warm toning
    // mild desaturate shadows (luminance < 80)
    uint8_t y = (uint8_t)(0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]);
    if (y < 80) {
        p[0] = (uint8_t)(p[0] * 0.85f + y * 0.15f);
        p[1] = (uint8_t)(p[1] * 0.85f + y * 0.15f);
        p[2] = (uint8_t)(p[2] * 0.85f + y * 0.15f);
    }
}

static void apply_vintage(uint8_t* p) {
    apply_sepia(p);
    // reduce saturation further
    uint8_t y = (uint8_t)(0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]);
    p[0] = (uint8_t)(p[0] * 0.7f + y * 0.3f);
    p[1] = (uint8_t)(p[1] * 0.7f + y * 0.3f);
    p[2] = (uint8_t)(p[2] * 0.7f + y * 0.3f);
    // slight luminance noise (deterministic, no rand to avoid seed issues)
    int noise = ((p[0] ^ p[1] ^ p[2]) & 0x0F) - 8;
    apply_brightness(p, noise);
}

// ─── Main filter dispatch ────────────────────────────────────
void filter_apply(camera_fb_t* fb, FilterPreset preset, int brightness_delta) {
    if (!fb || !fb->buf || fb->len == 0) return;
    if (preset == FILTER_NONE) return;

    // Decode JPEG → RGB888
    size_t rgb_len = fb->width * fb->height * 3;
    uint8_t* rgb = (uint8_t*)heap_caps_malloc(rgb_len,
                    psramFound() ? MALLOC_CAP_SPIRAM : MALLOC_CAP_DEFAULT);
    if (!rgb) {
        Serial.println(F("[FILTER] malloc failed — skipping filter"));
        return;
    }

    bool ok = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb);
    if (!ok) {
        Serial.println(F("[FILTER] JPEG decode failed"));
        heap_caps_free(rgb);
        return;
    }

    // Apply pixel transform
    size_t npix = (size_t)fb->width * fb->height;
    for (size_t i = 0; i < npix; i++) {
        uint8_t* p = rgb + i * 3;
        switch (preset) {
            case FILTER_GRAYSCALE:  apply_grayscale(p);                break;
            case FILTER_SEPIA:      apply_sepia(p);                    break;
            case FILTER_CINEMATIC:  apply_cinematic(p);                break;
            case FILTER_VINTAGE:    apply_vintage(p);                  break;
            case FILTER_WARM:       apply_warm(p);                     break;
            case FILTER_COOL:       apply_cool(p);                     break;
            case FILTER_BRIGHTNESS: apply_brightness(p, brightness_delta); break;
            default: break;
        }
    }

    // Re-encode RGB888 → JPEG back into fb->buf
    uint8_t* out_buf = nullptr;
    size_t   out_len = 0;
    bool enc_ok = fmt2jpg(rgb, rgb_len, fb->width, fb->height,
                          PIXFORMAT_RGB888, 12, &out_buf, &out_len);
    heap_caps_free(rgb);

    if (!enc_ok || !out_buf) {
        Serial.println(F("[FILTER] JPEG re-encode failed — returning raw"));
        return;
    }

    // Replace fb buffer content (only if new buffer fits)
    if (out_len <= fb->len) {
        memcpy(fb->buf, out_buf, out_len);
        fb->len = out_len;
    } else {
        Serial.println(F("[FILTER] Re-encoded size larger than original — skipping replace"));
    }
    free(out_buf);
}

// ─── Name helper ─────────────────────────────────────────────
const char* filter_name(FilterPreset p) {
    switch (p) {
        case FILTER_NONE:        return "none";
        case FILTER_GRAYSCALE:   return "grayscale";
        case FILTER_SEPIA:       return "sepia";
        case FILTER_CINEMATIC:   return "cinematic";
        case FILTER_VINTAGE:     return "vintage";
        case FILTER_WARM:        return "warm";
        case FILTER_COOL:        return "cool";
        case FILTER_BRIGHTNESS:  return "brightness";
        default:                 return "unknown";
    }
}
