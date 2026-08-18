#include "ValveTimer.h"

#include "ConfigStore.h"
#include "Logger.h"

namespace {

// Index 0 ungenutzt (V0 hat keinen eigenen Timer).
uint16_t remainingSeconds[6] = {0};

uint16_t effectiveSeconds(uint8_t index) {
  const uint16_t timeMinutes = ConfigStore::getValveTime(index);
  const uint16_t maxTimeMinutes = ConfigStore::getMaxTime();
  const uint16_t effectiveMinutes = timeMinutes < maxTimeMinutes ? timeMinutes : maxTimeMinutes;
  return effectiveMinutes * 60;
}

}  // namespace

void ValveTimer::begin() {
  for (uint8_t i = 1; i <= 5; i++) {
    remainingSeconds[i] = effectiveSeconds(i);
  }
}

void ValveTimer::start(uint8_t index) {
  if (index < 1 || index > 5) {
    return;
  }
  remainingSeconds[index] = effectiveSeconds(index);
}

void ValveTimer::reset(uint8_t index) {
  if (index < 1 || index > 5) {
    return;
  }
  remainingSeconds[index] = effectiveSeconds(index);
}

void ValveTimer::clear(uint8_t index) {
  if (index < 1 || index > 5) {
    return;
  }
  remainingSeconds[index] = 0;
}

uint8_t ValveTimer::tick(uint8_t activeMask) {
  uint8_t expiredMask = 0;
  for (uint8_t i = 1; i <= 5; i++) {
    if ((activeMask & (1 << i)) && remainingSeconds[i] > 0) {
      remainingSeconds[i]--;
      if (remainingSeconds[i] == 0) {
        expiredMask |= (1 << i);
        Logger::logf(Logger::Type::INFO, Logger::Source::VALVE, "V%u Zeit abgelaufen", i);
      }
    }
  }
  return expiredMask;
}

uint16_t ValveTimer::getRemainingSeconds(uint8_t index) {
  if (index < 1 || index > 5) {
    return 0;
  }
  return remainingSeconds[index];
}
