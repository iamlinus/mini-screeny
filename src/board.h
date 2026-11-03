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

// Physical panel (ST7789 framebuffer) size
#define PANEL_W 240
#define PANEL_H 280

// How the board is mounted. All pages draw on a canvas rotated by MOUNT_ROT (Arduino_GFX rotation),
// so logical "down" is the mounted floor. 3 = the panel's right edge (+X in panel pixels) is the floor
// (verified 2026-11-03: gravity along panel +X with the board in its mount). Calibration is stored in
// panel axes, so changing this does not need a recalibration.
#define MOUNT_ROT 3

// Logical screen size, as seen in the mount
#if MOUNT_ROT & 1
#define SCREEN_W PANEL_H
#define SCREEN_H PANEL_W
#else
#define SCREEN_W PANEL_W
#define SCREEN_H PANEL_H
#endif

// Index into the canvas framebuffer for logical pixel (x, y). Same mapping as Arduino_Canvas::writePixelPreclipped.
static inline int fbIndex(int x, int y) {
#if MOUNT_ROT == 1
  return x * PANEL_W + (SCREEN_H - 1 - y);
#elif MOUNT_ROT == 2
  return (SCREEN_H - 1 - y) * PANEL_W + (SCREEN_W - 1 - x);
#elif MOUNT_ROT == 3
  return (SCREEN_W - 1 - x) * PANEL_W + y;
#else
  return y * PANEL_W + x;
#endif
}

// LEDC channels: buzzer and backlight on different timers (ch/2 = timer)
#define LEDC_BUZZER 4
#define LEDC_BL     6

extern Arduino_GFX *gfx;
extern Arduino_Canvas *canvas;

void backlight(uint8_t level);
