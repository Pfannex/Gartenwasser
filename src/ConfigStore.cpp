#include "ConfigStore.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <cstring>

#include "Logger.h"

namespace {

constexpr const char *kConfigPath = "/config.json";
constexpr uint16_t kDefaultValveTimeMinutes = 5;
constexpr uint16_t kDefaultMaxTimeMinutes = 60;
constexpr bool kDefaultValveAuto = false;
constexpr const char *kDefaultValveAlias = "";

// Groesse des JSON-Speicherpools (Baumstruktur + kopierte Alias-Strings beim
// Laden). Grosszuegig bemessen: time/auto (je 5 kleine Werte) + bis zu 5
// Alias-Strings (kAliasMaxLength Zeichen) + maxTime.
constexpr size_t kJsonDocCapacity = 768;

// Index 0 (V0) hat keine eigene Laufzeit/Automatik, aber einen Alias.
uint16_t valveTimeMinutes[6] = {0, kDefaultValveTimeMinutes, kDefaultValveTimeMinutes, kDefaultValveTimeMinutes,
                                 kDefaultValveTimeMinutes, kDefaultValveTimeMinutes};
bool valveAuto[6] = {kDefaultValveAuto, kDefaultValveAuto, kDefaultValveAuto, kDefaultValveAuto,
                      kDefaultValveAuto, kDefaultValveAuto};
char valveAlias[6][ConfigStore::kAliasMaxLength + 1] = {{0}};
uint16_t maxTimeMinutes = kDefaultMaxTimeMinutes;

// Baut die JSON-Struktur (identisch fuer SPIFFS-Persistenz und main/config/state).
void buildJson(JsonDocument &doc) {
  JsonObject time = doc.createNestedObject("time");
  JsonObject autoFlags = doc.createNestedObject("auto");
  JsonObject alias = doc.createNestedObject("alias");
  for (uint8_t i = 1; i <= 5; i++) {
    char key[3];
    snprintf(key, sizeof(key), "V%u", i);
    time[key] = valveTimeMinutes[i];
    autoFlags[key] = valveAuto[i];
    alias[key] = valveAlias[i];
  }
  alias["V0"] = valveAlias[0];
  doc["maxTime"] = maxTimeMinutes;
}

void save() {
  StaticJsonDocument<kJsonDocCapacity> doc;
  buildJson(doc);

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

  StaticJsonDocument<kJsonDocCapacity> doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: JSON-Fehler beim Laden: %s", err.c_str());
    return;
  }

  JsonObjectConst time = doc["time"];
  JsonObjectConst autoFlags = doc["auto"];
  JsonObjectConst alias = doc["alias"];
  for (uint8_t i = 1; i <= 5; i++) {
    char key[3];
    snprintf(key, sizeof(key), "V%u", i);
    valveTimeMinutes[i] = time[key] | kDefaultValveTimeMinutes;
    valveAuto[i] = autoFlags[key] | kDefaultValveAuto;
    const char *aliasValue = alias[key] | kDefaultValveAlias;
    strncpy(valveAlias[i], aliasValue, ConfigStore::kAliasMaxLength);
    valveAlias[i][ConfigStore::kAliasMaxLength] = '\0';
  }
  const char *v0AliasValue = alias["V0"] | kDefaultValveAlias;
  strncpy(valveAlias[0], v0AliasValue, ConfigStore::kAliasMaxLength);
  valveAlias[0][ConfigStore::kAliasMaxLength] = '\0';
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

const char *ConfigStore::getValveAlias(uint8_t index) {
  if (index > 5) {
    return "";
  }
  return valveAlias[index];
}

void ConfigStore::setValveAlias(uint8_t index, const char *alias) {
  if (index > 5) {
    return;
  }
  strncpy(valveAlias[index], alias, kAliasMaxLength);
  valveAlias[index][kAliasMaxLength] = '\0';
  save();
}

size_t ConfigStore::toJson(char *buffer, size_t bufferSize) {
  StaticJsonDocument<kJsonDocCapacity> doc;
  buildJson(doc);
  return serializeJson(doc, buffer, bufferSize);
}
