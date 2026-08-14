/**
 * @file    HmiManager.h
 * @brief   Display (ST7789/SPI), Touch (AXS5106L/I2C) und LVGL.
 * @hardware ESP32-C6, ST7789-Display (SPI), AXS5106L Touch (I2C)
 */

#pragma once

class HmiManager {
 public:
  HmiManager() = delete;

  /// Initialisiert Display, Touch (inkl. Wire.begin() fuer den I2C-Bus) und LVGL.
  static void begin();

  /// Muss regelmaessig aus loop() aufgerufen werden (LVGL-Verarbeitung).
  static void loop();
};
