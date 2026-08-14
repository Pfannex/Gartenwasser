#include "Logger.h"

#include <Arduino.h>
#include <cstdarg>

namespace {

const char *typeToString(Logger::Type type) {
  switch (type) {
    case Logger::Type::ERROR: return "ERROR";
    case Logger::Type::INFO:  return "INFO ";
    case Logger::Type::DEBUG: return "DEBUG";
  }
  return "?????";
}

const char *sourceToString(Logger::Source source) {
  switch (source) {
    case Logger::Source::WIFI: return "WIFI ";
    case Logger::Source::MQTT: return "MQTT ";
    case Logger::Source::I2C:  return "I2C  ";
    case Logger::Source::HMI:  return "HMI  ";
  }
  return "?????";
}

}  // namespace

void Logger::log(Type type, Source source, const char *message) {
  const unsigned long ms = millis();
  const unsigned long hh = ms / 3600000UL;
  const unsigned long mm = (ms / 60000UL) % 60UL;
  const unsigned long ss = (ms / 1000UL) % 60UL;
  const unsigned long msPart = ms % 1000UL;
  Serial.printf("%02lu:%02lu:%02lu:%03lu %s %s %s\n", hh, mm, ss, msPart,
                typeToString(type), sourceToString(source), message);
}

void Logger::logf(Type type, Source source, const char *format, ...) {
  char buffer[192];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  log(type, source, buffer);
}
