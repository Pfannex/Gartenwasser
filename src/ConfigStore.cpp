#include "ConfigStore.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <cstring>

#include "Logger.h"

namespace {

constexpr const char *kConfigPath = "/config.json";
constexpr const char *kProgramsPath = "/programs.json";
constexpr uint16_t kDefaultValveTimeMinutes = 5;
constexpr uint16_t kDefaultMaxTimeMinutes = 60;
constexpr bool kDefaultValveAuto = false;
constexpr const char *kDefaultValveAlias = "";

// Index 0 (V0) hat keine eigene Laufzeit/Automatik, aber einen Alias.
uint16_t valveTimeMinutes[6] = {0, kDefaultValveTimeMinutes, kDefaultValveTimeMinutes, kDefaultValveTimeMinutes,
                                 kDefaultValveTimeMinutes, kDefaultValveTimeMinutes};
bool valveAuto[6] = {kDefaultValveAuto, kDefaultValveAuto, kDefaultValveAuto, kDefaultValveAuto,
                      kDefaultValveAuto, kDefaultValveAuto};
char valveAlias[6][ConfigStore::kAliasMaxLength + 1] = {{0}};
uint16_t maxTimeMinutes = kDefaultMaxTimeMinutes;

// Ein gespeichertes Programm (Phase 14). `time`/`autoFlag` sind nur gueltig, wenn das
// entsprechende Bit in `timeSetMask`/`autoSetMask` gesetzt ist (Bit i = Ventil i, 1..5) -
// Programme sind Teilmengen wie main/config/set, siehe docs/spec/14-programme.md.
struct StoredProgram {
  char name[ConfigStore::kAliasMaxLength + 1];
  uint8_t shortcut;  // 0 = keiner, 1..4 = P1..P4
  uint16_t time[6];
  bool autoFlag[6];
  uint8_t timeSetMask;
  uint8_t autoSetMask;
};

StoredProgram programs[ConfigStore::kMaxPrograms];
uint8_t programCount = 0;
uint8_t activeProgram = 0;

// String-Label je Shortcut-Wert (1..4), Index 0 ungenutzt. Literale mit statischer
// Speicherdauer - unbedenklich als const char* in ein JsonDocument zu schreiben
// (kein Dangling-Pointer-Risiko wie bei einem lokalen Stack-Puffer je Schleifendurchlauf).
constexpr const char *kShortcutLabels[5] = {"", "P1", "P2", "P3", "P4"};

// Erkennt Keys der Form "V<digit>" (Ventil 1..5) fuer das Laden von programs.json.
bool parseProgramValveKey(const char *key, uint8_t *outIndex) {
  if (key[0] != 'V' || key[1] < '1' || key[1] > '5' || key[2] != '\0') {
    return false;
  }
  *outIndex = static_cast<uint8_t>(key[1] - '0');
  return true;
}

// Wandelt "P1".."P4" in 1..4 um, alles andere (falscher String, fehlend) liefert 0
// (kein Shortcut) - wird beim Laden nicht abgelehnt, siehe docs/spec/14-programme.md,
// Kernentscheidung 8.
uint8_t parseShortcutLabel(const char *value) {
  if (value == nullptr || value[0] != 'P' || value[1] < '1' || value[1] > '4' || value[2] != '\0') {
    return 0;
  }
  return static_cast<uint8_t>(value[1] - '0');
}

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
  StaticJsonDocument<ConfigStore::kJsonCapacity> doc;
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

  StaticJsonDocument<ConfigStore::kJsonCapacity> doc;
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

// Baut die JSON-Struktur fuer programs.json / main/programs/state (identisch fuer beide).
void buildProgramsJson(JsonDocument &doc) {
  JsonArray arr = doc.createNestedArray("programs");
  for (uint8_t p = 0; p < programCount; p++) {
    JsonObject obj = arr.createNestedObject();
    obj["name"] = programs[p].name;
    if (programs[p].shortcut >= 1 && programs[p].shortcut <= 4) {
      obj["shortcut"] = kShortcutLabels[programs[p].shortcut];
    }
    JsonObject time = obj.createNestedObject("time");
    JsonObject autoFlags = obj.createNestedObject("auto");
    for (uint8_t i = 1; i <= 5; i++) {
      char key[3];
      snprintf(key, sizeof(key), "V%u", i);
      if (programs[p].timeSetMask & (1 << i)) {
        time[key] = programs[p].time[i];
      }
      if (programs[p].autoSetMask & (1 << i)) {
        autoFlags[key] = programs[p].autoFlag[i];
      }
    }
  }
  doc["activeProgram"] = activeProgram;
}

void saveProgramsFile() {
  StaticJsonDocument<ConfigStore::kProgramsJsonCapacity> doc;
  buildProgramsJson(doc);

  File file = SPIFFS.open(kProgramsPath, FILE_WRITE);
  if (!file) {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: programs.json nicht schreibbar.");
    return;
  }
  serializeJson(doc, file);
  file.close();
}

void loadProgramsFile() {
  programCount = 0;
  activeProgram = 0;
  if (!SPIFFS.exists(kProgramsPath)) {
    return;  // keine gespeicherten Programme -> leere Liste bleibt aktiv
  }

  File file = SPIFFS.open(kProgramsPath, FILE_READ);
  if (!file) {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: programs.json nicht lesbar.");
    return;
  }

  StaticJsonDocument<ConfigStore::kProgramsJsonCapacity> doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: JSON-Fehler in programs.json: %s",
                 err.c_str());
    return;
  }

  JsonArrayConst arr = doc["programs"];
  uint8_t p = 0;
  for (JsonObjectConst obj : arr) {
    if (p >= ConfigStore::kMaxPrograms) {
      break;
    }
    const char *name = obj["name"] | "";
    strncpy(programs[p].name, name, ConfigStore::kAliasMaxLength);
    programs[p].name[ConfigStore::kAliasMaxLength] = '\0';
    programs[p].shortcut = parseShortcutLabel(obj["shortcut"] | "");
    programs[p].timeSetMask = 0;
    programs[p].autoSetMask = 0;

    JsonObjectConst timeObj = obj["time"];
    for (JsonPairConst kv : timeObj) {
      uint8_t idx = 0;
      if (parseProgramValveKey(kv.key().c_str(), &idx)) {
        programs[p].time[idx] = kv.value().as<uint16_t>();
        programs[p].timeSetMask |= (1 << idx);
      }
    }
    JsonObjectConst autoObj = obj["auto"];
    for (JsonPairConst kv : autoObj) {
      uint8_t idx = 0;
      if (parseProgramValveKey(kv.key().c_str(), &idx)) {
        programs[p].autoFlag[idx] = kv.value().as<bool>();
        programs[p].autoSetMask |= (1 << idx);
      }
    }
    p++;
  }
  programCount = p;
  activeProgram = doc["activeProgram"] | 0;
}

}  // namespace

void ConfigStore::begin() {
  if (!SPIFFS.begin(true)) {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: SPIFFS-Mount fehlgeschlagen.");
    return;
  }
  load();
  loadProgramsFile();
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
  StaticJsonDocument<kJsonCapacity> doc;
  buildJson(doc);
  return serializeJson(doc, buffer, bufferSize);
}

void ConfigStore::setPrograms(const ProgramInput *entries, uint8_t count) {
  if (count > kMaxPrograms) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::SYSTEM,
                 "ConfigStore: %u Programme angefragt, nur %u erlaubt - Rest wird ignoriert.", count, kMaxPrograms);
    count = kMaxPrograms;
  }
  for (uint8_t p = 0; p < count; p++) {
    strncpy(programs[p].name, entries[p].name, kAliasMaxLength);
    programs[p].name[kAliasMaxLength] = '\0';
    programs[p].shortcut = entries[p].shortcut;
    programs[p].timeSetMask = 0;
    programs[p].autoSetMask = 0;
    for (uint8_t i = 1; i <= 5; i++) {
      if (entries[p].timeSet[i]) {
        programs[p].time[i] = entries[p].time[i];
        programs[p].timeSetMask |= (1 << i);
      }
      if (entries[p].autoSet[i]) {
        programs[p].autoFlag[i] = entries[p].autoFlag[i];
        programs[p].autoSetMask |= (1 << i);
      }
    }
  }
  programCount = count;

  // Duplikat-Erkennung (siehe docs/spec/14-programme.md, Kernentscheidung 8): wird
  // nicht abgelehnt (widerspraeche dem "ganzes Array ersetzen"-Prinzip), nur geloggt -
  // getProgramIndexForShortcut() liefert bei Duplikaten den ersten Treffer.
  for (uint8_t p = 0; p < programCount; p++) {
    if (programs[p].shortcut == 0) {
      continue;
    }
    for (uint8_t q = p + 1; q < programCount; q++) {
      if (programs[q].shortcut == programs[p].shortcut) {
        // Kurz gehalten, damit die Meldung im 96-Byte-lastError-Puffer (inkl. Zeitstempel-
        // Praefix) nicht abgeschnitten wird (siehe Diagnostics::lastError).
        Logger::logf(Logger::Type::ERROR, Logger::Source::SYSTEM, "Shortcut P%u doppelt belegt!",
                     programs[p].shortcut);
      }
    }
  }

  saveProgramsFile();
}

uint8_t ConfigStore::getProgramCount() {
  return programCount;
}

const char *ConfigStore::getProgramName(uint8_t programIndex) {
  if (programIndex < 1 || programIndex > programCount) {
    return "";
  }
  return programs[programIndex - 1].name;
}

bool ConfigStore::programHasTime(uint8_t programIndex, uint8_t valveIndex) {
  if (programIndex < 1 || programIndex > programCount || valveIndex < 1 || valveIndex > 5) {
    return false;
  }
  return programs[programIndex - 1].timeSetMask & (1 << valveIndex);
}

uint16_t ConfigStore::getProgramTime(uint8_t programIndex, uint8_t valveIndex) {
  if (!programHasTime(programIndex, valveIndex)) {
    return 0;
  }
  return programs[programIndex - 1].time[valveIndex];
}

bool ConfigStore::programHasAuto(uint8_t programIndex, uint8_t valveIndex) {
  if (programIndex < 1 || programIndex > programCount || valveIndex < 1 || valveIndex > 5) {
    return false;
  }
  return programs[programIndex - 1].autoSetMask & (1 << valveIndex);
}

bool ConfigStore::getProgramAuto(uint8_t programIndex, uint8_t valveIndex) {
  if (!programHasAuto(programIndex, valveIndex)) {
    return false;
  }
  return programs[programIndex - 1].autoFlag[valveIndex];
}

uint8_t ConfigStore::getActiveProgram() {
  return activeProgram;
}

void ConfigStore::setActiveProgram(uint8_t programIndex) {
  activeProgram = programIndex;
  saveProgramsFile();
}

uint8_t ConfigStore::getProgramIndexForShortcut(uint8_t shortcut) {
  if (shortcut < 1 || shortcut > 4) {
    return 0;
  }
  for (uint8_t p = 0; p < programCount; p++) {
    if (programs[p].shortcut == shortcut) {
      return p + 1;
    }
  }
  return 0;
}

size_t ConfigStore::programsToJson(char *buffer, size_t bufferSize) {
  StaticJsonDocument<kProgramsJsonCapacity> doc;
  buildProgramsJson(doc);
  return serializeJson(doc, buffer, bufferSize);
}
