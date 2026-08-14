/**
 * @file    Logger.h
 * @brief   Einheitliches Log-Format fuer alle Subsysteme.
 *
 * Zeilenformat: hh:mm:ss:mmm TYPE CLASS logtext
 * hh:mm:ss:mmm ist die Laufzeit seit Boot (millis()-basiert, keine Echtzeituhr).
 */

#pragma once

#include <cstdint>

class Logger {
 public:
  enum class Type : uint8_t { ERROR, INFO, DEBUG };
  enum class Source : uint8_t { WIFI, MQTT, I2C, HMI };

  Logger() = delete;

  static void log(Type type, Source source, const char *message);
  static void logf(Type type, Source source, const char *format, ...);
};
