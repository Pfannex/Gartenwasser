#include "OTA.h"

#include <ArduinoOTA.h>

#include "Logger.h"

namespace {

constexpr const char *kHostname = "gartenwasser";

const char *otaErrorToString(ota_error_t error) {
  switch (error) {
    case OTA_AUTH_ERROR:    return "Auth-Fehler";
    case OTA_BEGIN_ERROR:   return "Begin-Fehler";
    case OTA_CONNECT_ERROR: return "Verbindungsfehler";
    case OTA_RECEIVE_ERROR: return "Empfangsfehler";
    case OTA_END_ERROR:     return "End-Fehler";
  }
  return "unbekannter Fehler";
}

}  // namespace

void OTA::begin() {
  // Kein Passwort (setPassword()) - passend zum bisherigen Sicherheitsniveau des Projekts,
  // weder MQTT noch der WebIF-Upload (siehe WebIF.cpp) haben eine Authentifizierung.
  ArduinoOTA.setHostname(kHostname);
  ArduinoOTA.onStart([]() {
    const char *target = ArduinoOTA.getCommand() == U_FLASH ? "Firmware" : "Dateisystem";
    Logger::logf(Logger::Type::INFO, Logger::Source::OTA, "PlatformIO-OTA gestartet (%s).", target);
  });
  ArduinoOTA.onEnd([]() {
    Logger::log(Logger::Type::INFO, Logger::Source::OTA, "PlatformIO-OTA abgeschlossen, Neustart folgt.");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::OTA, "PlatformIO-OTA fehlgeschlagen: %s",
                 otaErrorToString(error));
  });
  ArduinoOTA.begin();
  Logger::logf(Logger::Type::INFO, Logger::Source::OTA, "PlatformIO-OTA bereit ('%s.local').", kHostname);
}

void OTA::loop() {
  ArduinoOTA.handle();
}
