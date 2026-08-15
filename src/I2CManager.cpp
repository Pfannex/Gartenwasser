#include "I2CManager.h"

#include <Wire.h>

#include "Logger.h"

namespace {
constexpr uint8_t kIodirB = 0x01;  ///< Richtungsregister Port B (1 = Eingang, 0 = Ausgang)
constexpr uint8_t kGpioB = 0x13;   ///< Pegel-Register Port B
}  // namespace

void I2CManager::writeRegister(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

void I2CManager::scan() {
  Logger::log(Logger::Type::INFO, Logger::Source::I2C, "I2C-Scan gestartet...");
  uint8_t found = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Logger::logf(Logger::Type::INFO, Logger::Source::I2C, "Geraet gefunden: 0x%02X", addr);
      found++;
    }
  }

  if (found == 0) {
    Logger::log(Logger::Type::INFO, Logger::Source::I2C, "Keine I2C-Geraete gefunden.");
  } else {
    Logger::logf(Logger::Type::INFO, Logger::Source::I2C, "I2C-Scan abgeschlossen: %u Geraet(e) gefunden.", found);
  }

  Wire.beginTransmission(kMcp23017Addr);
  if (Wire.endTransmission() == 0) {
    Logger::logf(Logger::Type::INFO, Logger::Source::I2C, "MCP23017 auf 0x%02X erreichbar.", kMcp23017Addr);
  } else {
    Logger::logf(Logger::Type::ERROR, Logger::Source::I2C, "MCP23017 NICHT auf 0x%02X gefunden!", kMcp23017Addr);
  }
}

void I2CManager::mcp23017Setup() {
  writeRegister(kMcp23017Addr, kIodirB, 0x00);  // alle Ausgang
  writeRegister(kMcp23017Addr, kGpioB, 0x00);   // alle LOW
}

bool I2CManager::isMcp23017Reachable() {
  Wire.beginTransmission(kMcp23017Addr);
  return Wire.endTransmission() == 0;
}
