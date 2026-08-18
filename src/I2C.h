/**
 * @file    I2C.h
 * @brief   I2C-Bus-Funktionen (Scan, Register-Zugriff, MCP23017-Grundsetup).
 * @note    Wire muss vor Aufruf dieser Funktionen bereits initialisiert sein
 *          (aktuell durch HMI::begin(), da Touch und MCP23017 sich den
 *          Bus teilen).
 */

#pragma once

#include <cstdint>

class I2C {
 public:
  static constexpr uint8_t kMcp23017Addr = 0x20;  ///< I2C-Adresse bei A0-A2 = GND

  I2C() = delete;

  /// Durchsucht den I2C-Bus (Adressen 1-126) und prueft gezielt den MCP23017.
  static void scan();

  /// Schreibt ein Register-Byte per I2C an ein Geraet.
  static void writeRegister(uint8_t addr, uint8_t reg, uint8_t value);

  /// Konfiguriert den MCP23017 (Port B, alle Pins Ausgang, LOW).
  static void mcp23017Setup();

  /// Prueft, ob der MCP23017 aktuell auf dem I2C-Bus antwortet.
  static bool isMcp23017Reachable();
};
