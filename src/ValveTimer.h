/**
 * @file    ValveTimer.h
 * @brief   Countdown der Restlaufzeit je Ventil (V1-V5), Werte aus ConfigStore.
 */

#pragma once

#include <cstdint>

class ValveTimer {
 public:
  ValveTimer() = delete;

  /// Setzt alle Restlaufzeiten auf 0 (kein Ventil laeuft nach Boot, siehe ValveController).
  static void begin();

  /// Startet den Countdown fuer Ventil `index` (1..5) mit min(time, maxTime) aus ConfigStore.
  static void start(uint8_t index);

  /// Bricht den Countdown fuer Ventil `index` ab (Restlaufzeit auf 0).
  static void stop(uint8_t index);

  /// Zaehlt alle aktiven Countdowns um eine Sekunde herunter. Muss sekuendlich
  /// aufgerufen werden. Liefert eine Bitmaske (Bit `index`) der Ventile, deren
  /// Countdown in diesem Aufruf 0 erreicht hat.
  static uint8_t tick();

  /// Liefert die aktuelle Restlaufzeit in Sekunden fuer Ventil `index`.
  static uint16_t getRemainingSeconds(uint8_t index);
};
