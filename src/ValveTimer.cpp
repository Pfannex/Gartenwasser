#include "ValveTimer.h"

#include "ConfigStore.h"

namespace {

// Index 0 ungenutzt (V0 hat keinen eigenen Timer).
uint16_t remainingSeconds[6] = {0};

}  // namespace

void ValveTimer::begin() {
  for (uint8_t i = 0; i < 6; i++) {
    remainingSeconds[i] = 0;
  }
}

void ValveTimer::start(uint8_t index) {
  if (index < 1 || index > 5) {
    return;
  }
  const uint16_t timeMinutes = ConfigStore::getValveTime(index);
  const uint16_t maxTimeMinutes = ConfigStore::getMaxTime();
  const uint16_t effectiveMinutes = timeMinutes < maxTimeMinutes ? timeMinutes : maxTimeMinutes;
  remainingSeconds[index] = effectiveMinutes * 60;
}

void ValveTimer::stop(uint8_t index) {
  if (index < 1 || index > 5) {
    return;
  }
  remainingSeconds[index] = 0;
}

uint8_t ValveTimer::tick() {
  uint8_t expiredMask = 0;
  for (uint8_t i = 1; i <= 5; i++) {
    if (remainingSeconds[i] > 0) {
      remainingSeconds[i]--;
      if (remainingSeconds[i] == 0) {
        expiredMask |= (1 << i);
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
