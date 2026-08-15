/**
 * @file    ConfigStore.h
 * @brief   Persistenz der Einstellwerte (aktuell: Ventil-Laufzeiten, maxTime) im SPIFFS.
 */

#pragma once

#include <cstdint>

class ConfigStore {
 public:
  ConfigStore() = delete;

  /// Mountet SPIFFS und laedt /config.json (falls vorhanden), sonst gelten Defaultwerte.
  static void begin();

  /// Konfigurierte Laufzeit in Minuten fuer Ventil `index` (1..5).
  static uint16_t getValveTime(uint8_t index);
  static void setValveTime(uint8_t index, uint16_t minutes);

  /// Globale Obergrenze in Minuten (main/time/maxTime, effektive Laufzeit = min(time, maxTime)).
  static uint16_t getMaxTime();
  static void setMaxTime(uint16_t minutes);

  /// Automatik-Flag fuer Ventil `index` (1..5): Teilnahme an der Automatik-Sequenz (Phase 7).
  static bool getValveAuto(uint8_t index);
  static void setValveAuto(uint8_t index, bool on);
};
