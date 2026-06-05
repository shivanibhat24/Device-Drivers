#include "camera_driver.h"
#include "../system/config.h"
#include <Arduino.h>

bool camera_init() {
    camera_config_t config;

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = CAM_PIN_D0;
    config.pin_d1       = CAM_PIN_D1;
    config.pin_d2       = CAM_PIN_D2;
    config.pin_d3       = CAM_PIN_D3;
    config.pin_d4       = CAM_PIN_D4;
    config.pin_d5       = CAM_PIN_D5;
    config.pin_d6       = CAM_PIN_D6;
    config.pin_d7       = CAM_PIN_D7;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_pclk     = CAM_PIN_PCLK;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_sscb_sda = CAM_PIN_SIOD;
    config.pin_sscb_scl = CAM_PIN_SIOC;
    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;

    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size   = FRAMESIZE_SVGA;  // 800×600 — good balance
    config.jpeg_quality = JPEG_QUALITY;
    config.fb_count     = 1;

    // Use PSRAM if available for larger buffers
    if (psramFound()) {
        config.frame_size   = FRAMESIZE_UXGA;  // 1600×1200 with PSRAM
        config.jpeg_quality = 10;
        config.fb_count     = 2;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM] esp_camera_init error 0x%x\n", err);
        return false;
    }

    // Sensor fine-tuning
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_wb_mode(s, 0);
        s->set_aec2(s, 0);
        s->set_ae_level(s, 0);
        s->set_aec_value(s, 300);
        s->set_gain_ctrl(s, 1);
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)0);
        s->set_bpc(s, 0);
        s->set_wpc(s, 1);
        s->set_raw_gma(s, 1);
        s->set_lenc(s, 1);
        s->set_hmirror(s, 0);
        s->set_vflip(s, 0);
        s->set_dcw(s, 1);
    }
    return true;
}
