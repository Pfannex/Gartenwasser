/**
 * @file    Logger.h
 * @brief   Einheitliches Log-Format fuer alle Subsysteme.
 *
 * Zeilenformat: hh:mm:ss:mmm CLASS TYPE logtext
 * hh:mm:ss:mmm ist boot-relative Laufzeit (millis()-basiert), bis enableRealTime()
 * nach erfolgreicher NTP-Synchronisierung auf Echtzeit umschaltet.
 */

#pragma once

#include <cstddef>
#include <cstdint>

class Logger {
 public:
  enum class Type : uint8_t { ERROR, INFO, DEBUG, PUB, SUB };
  // VALVE (2026-08-18 ergaenzt): tatsaechliche Ventilschaltungen (ValveController/ValveTimer) -
  // vorher faelschlich unter I2C mitgelaufen, das jetzt ausschliesslich Bus-Gesundheit meint
  // (Scan, MCP23017-Erreichbarkeit). SEQ: Automatik-Sequenz-Lebenszyklus (Sequencer), vorher
  // komplett ungeloggt.
  enum class Source : uint8_t { WIFI, MQTT, I2C, HMI, WEB, SYSTEM, VALVE, SEQ };

  using ErrorCallback = void (*)(const char *message);

  Logger() = delete;

  static void log(Type type, Source source, const char *message);
  static void logf(Type type, Source source, const char *format, ...);

  /// Schaltet den Zeitstempel von boot-relativer Zeit auf Echtzeit um (nach erfolgreicher NTP-Synchronisierung).
  static void enableRealTime();

  /// Liefert true, wenn die Systemzeit per NTP synchronisiert ist (siehe enableRealTime()).
  /// Fuer Code, der echtes Datum/Uhrzeit braucht (z.B. Scheduler, Phase 15) statt nur eines
  /// Log-Zeitstempels, der auch boot-relativ sinnvoll ist.
  static bool isRealTimeEnabled();

  /// Formatiert den aktuellen Zeitstempel (wie am Anfang jeder Logzeile) nach `buffer`.
  static void currentTimestamp(char *buffer, size_t bufferSize);

  /// Registriert einen Callback, der bei jedem ERROR-Log-Eintrag mit der reinen
  /// (unformatierten) Meldung aufgerufen wird - Grundlage fuer diagnostics/lastError
  /// (Diagnostics), ohne dass Logger die Diagnostics-Klasse kennen muss.
  static void setErrorCallback(ErrorCallback callback);
};
