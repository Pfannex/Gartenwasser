#include "ConfigStore.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>

#include "Logger.h"

namespace {

constexpr const char *kConfigPath = "/config.json";
constexpr uint16_t kDefaultValveTimeMinutes = 5;
constexpr uint16_t kDefaultMaxTimeMinutes = 60;
constexpr bool kDefaultValveAuto = false;

// Index 0 ungenutzt (V0 hat keine eigene Laufzeit/Automatik).
uint16_t valveTimeMinutes[6] = {0, kDefaultValveTimeMinutes, kDefaultValveTimeMinutes, kDefaultValveTimeMinutes,
                                 kDefaultValveTimeMinutes, kDefaultValveTimeMinutes};
bool valveAuto[6] = {kDefaultValveAuto, kDefaultValveAuto, kDefaultValveAuto, kDefaultValveAuto,
                      kDefaultValveAuto, kDefaultValveAuto};
uint16_t maxTimeMinutes = kDefaultMaxTimeMinutes;

void save() {
  StaticJsonDocument<384> doc;
  JsonObject time = doc.createNestedObject("time");
  JsonObject autoFlags = doc.createNestedObject("auto");
  for (uint8_t i = 1; i <= 5; i++) {
    char key[3];
    snprintf(key, sizeof(key), "V%u", i);
    time[key] = valveTimeMinutes[i];
    autoFlags[key] = valveAuto[i];
  }
  doc["maxTime"] = maxTimeMinutes;

  File file = SPIFFS.open(kConfigPath, FILE_WRITE);
  if (!file) {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: Datei zum Schreiben nicht oeffenbar.");
    return;
  }
  serializeJson(doc, file);
  file.close();
}

void load() {
  if (!SPIFFS.exists(kConfigPath)) {
    return;  // keine gespeicherte Konfiguration -> Defaults bleiben aktiv
  }

  File file = SPIFFS.open(kConfigPath, FILE_READ);
  if (!file) {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: Datei zum Lesen nicht oeffenbar.");
    return;
  }

  StaticJsonDocument<384> doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: JSON-Fehler beim Laden: %s", err.c_str());
    return;
  }

  JsonObjectConst time = doc["time"];
  JsonObjectConst autoFlags = doc["auto"];
  for (uint8_t i = 1; i <= 5; i++) {
    char key[3];
    snprintf(key, sizeof(key), "V%u", i);
    valveTimeMinutes[i] = time[key] | kDefaultValveTimeMinutes;
    valveAuto[i] = autoFlags[key] | kDefaultValveAuto;
  }
  maxTimeMinutes = doc["maxTime"] | kDefaultMaxTimeMinutes;
}

}  // namespace

void ConfigStore::begin() {
  if (!SPIFFS.begin(true)) {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: SPIFFS-Mount fehlgeschlagen.");
    return;
  }
  load();
}

uint16_t ConfigStore::getValveTime(uint8_t index) {
  if (index < 1 || index > 5) {
    return 0;
  }
  return valveTimeMinutes[index];
}

void ConfigStore::setValveTime(uint8_t index, uint16_t minutes) {
  if (index < 1 || index > 5) {
    return;
  }
  valveTimeMinutes[index] = minutes;
  save();
}

uint16_t ConfigStore::getMaxTime() {
  return maxTimeMinutes;
}

void ConfigStore::setMaxTime(uint16_t minutes) {
  maxTimeMinutes = minutes;
  save();
}

bool ConfigStore::getValveAuto(uint8_t index) {
  if (index < 1 || index > 5) {
    return false;
  }
  return valveAuto[index];
}

void ConfigStore::setValveAuto(uint8_t index, bool on) {
  if (index < 1 || index > 5) {
    return;
  }
  valveAuto[index] = on;
  save();
}
