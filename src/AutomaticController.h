/**
 * @file    AutomaticController.h
 * @brief   Reine Ablaufsteuerung fuer die Automatik-Sequenz (Warteschlange + Cursor).
 *          Kennt keine Ventile/MQTT - das Ein-/Ausschalten und Publizieren macht
 *          der Aufrufer (MQTT), analog zu ValveTimer.
 */

#pragma once

#include <cstdint>

class AutomaticController {
 public:
  AutomaticController() = delete;

  /// Setzt den Zustand zurueck (keine Sequenz aktiv). Fuer den Boot-Grundzustand.
  static void begin();

  /// Startet die Sequenz mit den uebergebenen Ventil-Indizes (1..5) in der
  /// gegebenen Reihenfolge. Liefert false (und aendert nichts) bei `count == 0`.
  static bool start(const uint8_t *valves, uint8_t count);

  /// Bricht eine laufende Sequenz ab (nur Zustand, schaltet keine Ventile).
  static void stop();

  static bool isRunning();

  /// Aktuell aktives Ventil (1..5), oder 0 wenn keine Sequenz laeuft.
  static uint8_t getActiveValve();

  /// Rueckt zum naechsten Ventil in der Warteschlange vor. Liefert das neue
  /// aktive Ventil (1..5), oder 0, wenn die Sequenz damit beendet ist.
  static uint8_t advance();

  /// Anzahl noch ausstehender Ventile (ohne das aktuell aktive), fuer main/remainingTotal.
  static uint8_t getPendingCount();

  /// Ventil-Index des `offset`-ten ausstehenden Ventils (0 = naechstes nach dem aktiven).
  static uint8_t getPendingValve(uint8_t offset);
};
