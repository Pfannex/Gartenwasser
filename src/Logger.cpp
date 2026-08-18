#include "Logger.h"

#include <Arduino.h>
#include <cstdarg>
#include <ctime>

namespace {

bool realTimeEnabled = false;
Logger::ErrorCallback errorCallback = nullptr;
Logger::LineCallback lineCallback = nullptr;

const char *typeToString(Logger::Type type) {
  switch (type) {
    case Logger::Type::ERROR: return "ERROR";
    case Logger::Type::INFO:  return "INFO ";
    case Logger::Type::DEBUG: return "DEBUG";
    case Logger::Type::PUB:   return "PUB  ";
    case Logger::Type::SUB:   return "SUB  ";
  }
  return "?????";
}

const char *sourceToString(Logger::Source source) {
  switch (source) {
    case Logger::Source::WIFI:   return "WIFI ";
    case Logger::Source::MQTT:   return "MQTT ";
    case Logger::Source::I2C:    return "I2C  ";
    case Logger::Source::HMI:    return "HMI  ";
    case Logger::Source::WEB:    return "WEB  ";
    case Logger::Source::SYSTEM: return "SYS  ";
    case Logger::Source::VALVE:  return "VALVE";
    case Logger::Source::SEQ:    return "SEQ  ";
  }
  return "?????";
}

}  // namespace

void Logger::enableRealTime() {
  realTimeEnabled = true;
}

bool Logger::isRealTimeEnabled() {
  return realTimeEnabled;
}

void Logger::currentTimestamp(char *buffer, size_t bufferSize) {
  if (realTimeEnabled) {
    const time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    snprintf(buffer, bufferSize, "%02d:%02d:%02d:%03lu", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             millis() % 1000UL);
  } else {
    const unsigned long ms = millis();
    const unsigned long hh = ms / 3600000UL;
    const unsigned long mm = (ms / 60000UL) % 60UL;
    const unsigned long ss = (ms / 1000UL) % 60UL;
    snprintf(buffer, bufferSize, "%02lu:%02lu:%02lu:%03lu", hh, mm, ss, ms % 1000UL);
  }
}

void Logger::setErrorCallback(ErrorCallback callback) {
  errorCallback = callback;
}

void Logger::setLineCallback(LineCallback callback) {
  lineCallback = callback;
}

void Logger::log(Type type, Source source, const char *message) {
  char timestamp[16];
  currentTimestamp(timestamp, sizeof(timestamp));
  char line[kMaxLineLength];
  snprintf(line, sizeof(line), "%s %s %s %s", timestamp, sourceToString(source), typeToString(type), message);
  Serial.println(line);

  if (type == Type::ERROR && errorCallback != nullptr) {
    errorCallback(message);
  }
  if (lineCallback != nullptr) {
    lineCallback(line);
  }
}

void Logger::logf(Type type, Source source, const char *format, ...) {
  // Nachtrag 2026-08-18: 192->512 Byte, damit z.B. "topic = payload" fuer main/config/state
  // (JSON-Publish, siehe MqttManager::publishAndLog()) nicht mehr mitten im String abgeschnitten
  // wird - Grund war urspruenglich das Pretty-Print im Live-Log, das abgeschnittenes JSON nicht
  // parsen kann. main/programs/state/schedule/state (potenziell mehrere KB) bleiben trotzdem
  // teils abgeschnitten, dafuer waere ein Zeilenformat grundsaetzlich der falsche Ansatz.
  // Bewusst kleiner als kMaxLineLength, um Platz fuer Zeitstempel/CLASS/TYPE-Praefix zu lassen.
  char buffer[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  log(type, source, buffer);
}
