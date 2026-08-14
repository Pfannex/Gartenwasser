/**
 * @file    Logger.h
 * @brief   Einheitliches Log-Format fuer alle Subsysteme.
 *
 * Zeilenformat: hh:mm:ss:mmm TYPE CLASS logtext
 * hh:mm:ss:mmm ist boot-relative Laufzeit (millis()-basiert), bis enableRealTime()
 * nach erfolgreicher NTP-Synchronisierung auf Echtzeit umschaltet.
 */

#pragma once

#include <cstdint>

class Logger {
 public:
  enum class Type : uint8_t { ERROR, INFO, DEBUG };
  enum class Source : uint8_t { WIFI, MQTT, I2C, HMI, SYSTEM };

  Logger() = delete;

  static void log(Type type, Source source, const char *message);
  static void logf(Type type, Source source, const char *format, ...);

  /// Schaltet den Zeitstempel von boot-relativer Zeit auf Echtzeit um (nach erfolgreicher NTP-Synchronisierung).
  static void enableRealTime();
};
