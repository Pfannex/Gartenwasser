#include "ValveController.h"

#include "ConfigStore.h"
#include "I2CManager.h"
#include "Logger.h"

namespace {

constexpr uint8_t kGpioB = 0x13;  ///< Pegel-Register Port B (MCP23017)

// Pin-Zuordnung Port B je Ventil (V0..V5), siehe docs/requirements.md.
constexpr uint8_t kValvePins[ValveController::kValveCount] = {7, 2, 3, 4, 5, 6};

// Schattenregister des zuletzt geschriebenen Port-B-Zustands (GPIOB ist fuer
// unsere Zwecke write-only: wir sind die einzige Quelle, die die Pins treibt).
uint8_t portBState = 0x00;

}  // namespace

void ValveController::begin() {
  portBState = 0x00;
  I2CManager::writeRegister(I2CManager::kMcp23017Addr, kGpioB, portBState);
}

void ValveController::setValve(uint8_t index, bool on) {
  if (index >= kValveCount) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::I2C, "ValveController: ungueltiger Index %u", index);
    return;
  }

  const uint8_t bit = kValvePins[index];
  if (on) {
    portBState |= (1 << bit);
  } else {
    portBState &= ~(1 << bit);
  }
  I2CManager::writeRegister(I2CManager::kMcp23017Addr, kGpioB, portBState);
  Logger::logf(Logger::Type::INFO, Logger::Source::I2C, "V%u %s", index, on ? "ON" : "OFF");
}

bool ValveController::getValve(uint8_t index) {
  if (index >= kValveCount) {
    return false;
  }
  return (portBState & (1 << kValvePins[index])) != 0;
}

bool ValveController::anyIrrigationValveActive() {
  for (uint8_t i = 1; i <= 5; i++) {
    if (getValve(i)) {
      return true;
    }
  }
  return false;
}

bool ValveController::getAuto(uint8_t index) {
  return ConfigStore::getValveAuto(index);
}

void ValveController::setAuto(uint8_t index, bool on) {
  ConfigStore::setValveAuto(index, on);
}

const char *ValveController::getAlias(uint8_t index) {
  return ConfigStore::getValveAlias(index);
}

void ValveController::setAlias(uint8_t index, const char *alias) {
  ConfigStore::setValveAlias(index, alias);
}
