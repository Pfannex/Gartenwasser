/**
 * @file    ValveController.h
 * @brief   Kapselt V0-V5 als MCP23017-Ausgaenge (Port B), unabhaengig von MQTT.
 */

#pragma once

#include <cstdint>

class ValveController {
 public:
  static constexpr uint8_t kValveCount = 6;  ///< V0 (Hauptventil) .. V5

  ValveController() = delete;

  /// Setzt alle Ventile auf AUS (sicherer Boot-Grundzustand).
  static void begin();

  /// Schaltet Ventil `index` (0=V0 .. 5=V5) ein/aus.
  static void setValve(uint8_t index, bool on);

  /// Liefert den zuletzt gesetzten Schaltzustand von Ventil `index`.
  static bool getValve(uint8_t index);

  /// Liefert true, wenn mindestens eines der Bewaesserungsventile V1-V5 eingeschaltet ist
  /// (fuer die V0-Kopplung: V0 bleibt an, solange irgendein Ventil aktiv ist).
  static bool anyIrrigationValveActive();

  /// Automatik-Flag von Ventil `index` (1..5): Teilnahme an der Automatik-Sequenz (Phase 7).
  /// Persistiert via ConfigStore, hat vorerst noch keine Funktion.
  static bool getAuto(uint8_t index);
  static void setAuto(uint8_t index, bool on);

  /// Klartextname (Alias) von Ventil `index` (0=V0 .. 5=V5). Persistiert via ConfigStore.
  static const char *getAlias(uint8_t index);
  static void setAlias(uint8_t index, const char *alias);
};
