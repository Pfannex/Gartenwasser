/**
 * @file    ValveTimer.h
 * @brief   Countdown der Restlaufzeit je Ventil (V1-V5), Werte aus FileSystem.
 *
 * Ein Ventil, das nicht laeuft, zeigt als Restlaufzeit seine konfigurierte
 * effektive Laufzeit (min(time, maxTime)) - so ist auf einen Blick erkennbar,
 * welche Ventile noch nicht gelaufen sind bzw. wieder "armiert" sind. Nur
 * waehrend das Ventil tatsaechlich eingeschaltet ist, zaehlt tick() herunter.
 */

#pragma once

#include <cstdint>

class ValveTimer {
 public:
  ValveTimer() = delete;

  /// Armiert alle Ventile mit ihrer konfigurierten effektiven Laufzeit (kein
  /// Ventil laeuft nach Boot, siehe ValveController).
  static void begin();

  /// Startet den Countdown fuer Ventil `index` (1..5) mit min(time, maxTime) aus FileSystem.
  static void start(uint8_t index);

  /// Setzt Ventil `index` auf die konfigurierte effektive Laufzeit zurueck (armiert) -
  /// nach Ausschalten ausserhalb einer laufenden Automatik-Sequenz, oder wenn sich
  /// `time`/`maxTime` waehrend des Leerlaufs aendert.
  static void reset(uint8_t index);

  /// Setzt die Restlaufzeit von Ventil `index` auf 0 - fuer ein Ventil, das innerhalb
  /// einer noch laufenden Automatik-Sequenz bereits durchgelaufen ist (nicht "armiert",
  /// sonst sieht es so aus, als waere es noch an der Reihe). Siehe reset() fuer den
  /// Gegenfall (Sequenz komplett beendet -> alle Ventile wieder auf Zeit).
  static void clear(uint8_t index);

  /// Zaehlt die Restlaufzeit aller Ventile herunter, deren Bit in `activeMask`
  /// gesetzt ist (Bit `index` = Ventil `index` ist eingeschaltet). Muss sekuendlich
  /// aufgerufen werden. Liefert eine Bitmaske der Ventile, deren Countdown in
  /// diesem Aufruf 0 erreicht hat.
  static uint8_t tick(uint8_t activeMask);

  /// Liefert die aktuelle Restlaufzeit in Sekunden fuer Ventil `index`.
  static uint16_t getRemainingSeconds(uint8_t index);
};
