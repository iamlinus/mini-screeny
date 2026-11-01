#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Waveshare ESP32-S3-LCD-1.69 (non-touch). Verified on hardware unless noted.
#define LCD_DC   4
#define LCD_CS   5
#define LCD_CLK  6
#define LCD_DIN  7
#define LCD_RST  8
#define LCD_BL   15
#define PWR_PIN  40   // PWR button (verified with probe)
#define BOOT_PIN 0    // BOOT button (runtime use not yet verified on this board)
#define BUZZER_PIN 42 // verified: user heard it
#define I2C_SDA  11
#define I2C_SCL  10
#define IMU_ADDR 0x6B // QMI8658, WHO_AM_I=0x05 verified

#define SCREEN_W 240
#define SCREEN_H 280

// LEDC channels: buzzer and backlight on different timers (ch/2 = timer)
#define LEDC_BUZZER 4
#define LEDC_BL     6

extern Arduino_GFX *gfx;
extern Arduino_Canvas *canvas;

void backlight(uint8_t level);
