/**
 * @file    ConfigStore.h
 * @brief   Persistenz der Einstellwerte (aktuell: Ventil-Laufzeiten, maxTime) im SPIFFS.
 */

#pragma once

#include <cstddef>
#include <cstdint>

class ConfigStore {
 public:
  /// Maximale Laenge (ohne Nullterminator) fuer einen Ventil-Alias.
  static constexpr size_t kAliasMaxLength = 32;

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

  /// Klartextname (Alias) fuer Ventil `index` (0..5, inkl. V0/Hauptventil). Leerer String,
  /// falls nicht gesetzt. `alias` wird auf kAliasMaxLength Zeichen gekuerzt uebernommen
  /// (Aufrufer validiert vorher).
  static const char *getValveAlias(uint8_t index);
  static void setValveAlias(uint8_t index, const char *alias);

  /// Serialisiert die aktuelle Gesamt-Konfiguration (time/auto/alias/maxTime) als JSON
  /// nach `buffer` (dieselbe Struktur wie /config.json). Liefert die Anzahl geschriebener
  /// Bytes (wie serializeJson()), oder 0 bei Fehler.
  static size_t toJson(char *buffer, size_t bufferSize);
};
