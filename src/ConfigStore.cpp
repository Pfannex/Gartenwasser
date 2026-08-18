#include "ConfigStore.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cstdlib>
#include <cstring>

#include "Logger.h"

namespace {

// Eigene LittleFS-Instanz auf einer eigenen Partition ("config", siehe partitions.csv) -
// getrennt von der "webfs"-Partition, die WebManager per `pio run --target uploadfs`
// bespielt. Verhindert, dass ein Dashboard-Update (uploadfs ueberschreibt die komplette
// Zielpartition) die persistierte Konfiguration mitloescht (siehe docs/spec/16-webif-
// fundament.md fuer den urspruenglichen, mit gemeinsamer Partition aufgetretenen Bug).
fs::LittleFSFS configFs;

constexpr const char *kConfigPath = "/config.json";
constexpr const char *kProgramsPath = "/programs.json";
constexpr const char *kSchedulePath = "/schedule.json";
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
  uint16_t time[6];
  bool autoFlag[6];
  uint8_t timeSetMask;
  uint8_t autoSetMask;
};

StoredProgram programs[ConfigStore::kMaxPrograms];
uint8_t programCount = 0;
uint8_t activeProgram = 0;

// Ein gespeicherter Zeitplan-Eintrag (Phase 15). `weekdaysMask` (Bit 0=Montag..6=Sonntag)
// nur bei type=WEEKLY relevant, `year`/`month`/`day` nur bei type=ONCE - siehe
// docs/spec/15-wochenplan.md.
struct StoredScheduleEntry {
  char program[ConfigStore::kAliasMaxLength + 1];
  bool enabled;
  ConfigStore::ScheduleType type;
  uint8_t hour;
  uint8_t minute;
  uint8_t weekdaysMask;
  uint16_t year;
  uint8_t month;
  uint8_t day;
};

StoredScheduleEntry schedule[ConfigStore::kMaxScheduleEntries];
uint8_t scheduleCount = 0;
bool scheduleGlobalEnabled = true;

// Kurzform je Wochentag (Bit-Index 0=Montag..6=Sonntag), statische Speicherdauer -
// sicher als const char* in ein JsonDocument zu schreiben.
constexpr const char *kWeekdayLabels[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
constexpr const char *kScheduleTypeLabels[3] = {"daily", "weekly", "once"};

// Erkennt Keys der Form "V<digit>" (Ventil 1..5) fuer das Laden von programs.json.
bool parseProgramValveKey(const char *key, uint8_t *outIndex) {
  if (key[0] != 'V' || key[1] < '1' || key[1] > '5' || key[2] != '\0') {
    return false;
  }
  *outIndex = static_cast<uint8_t>(key[1] - '0');
  return true;
}

// Wandelt "mon".."sun" in den Bit-Index 0..6 um, 0xFF bei unbekanntem Wert.
uint8_t parseWeekdayLabel(const char *value) {
  if (value == nullptr) {
    return 0xFF;
  }
  for (uint8_t i = 0; i < 7; i++) {
    if (strcmp(value, kWeekdayLabels[i]) == 0) {
      return i;
    }
  }
  return 0xFF;
}

// Wandelt "daily"/"weekly"/"once" in ConfigStore::ScheduleType um, liefert false bei
// unbekanntem Wert (Aufrufer entscheidet, wie damit umgegangen wird).
bool parseScheduleType(const char *value, ConfigStore::ScheduleType *outType) {
  if (value == nullptr) {
    return false;
  }
  if (strcmp(value, "daily") == 0) {
    *outType = ConfigStore::ScheduleType::DAILY;
    return true;
  }
  if (strcmp(value, "weekly") == 0) {
    *outType = ConfigStore::ScheduleType::WEEKLY;
    return true;
  }
  if (strcmp(value, "once") == 0) {
    *outType = ConfigStore::ScheduleType::ONCE;
    return true;
  }
  return false;
}

// Parst "HH:MM" (exakt 5 Zeichen, Doppelpunkt an Position 2) in Stunde/Minute.
bool parseTimeString(const char *value, uint8_t *outHour, uint8_t *outMinute) {
  if (value == nullptr || strlen(value) != 5 || value[2] != ':') {
    return false;
  }
  const char hourBuf[3] = {value[0], value[1], '\0'};
  const char minuteBuf[3] = {value[3], value[4], '\0'};
  char *end = nullptr;
  const long hour = strtol(hourBuf, &end, 10);
  if (*end != '\0' || hour < 0 || hour > 23) {
    return false;
  }
  const long minute = strtol(minuteBuf, &end, 10);
  if (*end != '\0' || minute < 0 || minute > 59) {
    return false;
  }
  *outHour = static_cast<uint8_t>(hour);
  *outMinute = static_cast<uint8_t>(minute);
  return true;
}

// Parst "YYYY-MM-DD" (ISO 8601, exakt 10 Zeichen) in Jahr/Monat/Tag. Prueft nur
// Wertebereiche (kein Kalender-Check, z.B. 31. Februar wird nicht abgelehnt).
bool parseDateString(const char *value, uint16_t *outYear, uint8_t *outMonth, uint8_t *outDay) {
  if (value == nullptr || strlen(value) != 10 || value[4] != '-' || value[7] != '-') {
    return false;
  }
  const char yearBuf[5] = {value[0], value[1], value[2], value[3], '\0'};
  const char monthBuf[3] = {value[5], value[6], '\0'};
  const char dayBuf[3] = {value[8], value[9], '\0'};
  char *end = nullptr;
  const long year = strtol(yearBuf, &end, 10);
  if (*end != '\0' || year < 2000 || year > 2099) {
    return false;
  }
  const long month = strtol(monthBuf, &end, 10);
  if (*end != '\0' || month < 1 || month > 12) {
    return false;
  }
  const long day = strtol(dayBuf, &end, 10);
  if (*end != '\0' || day < 1 || day > 31) {
    return false;
  }
  *outYear = static_cast<uint16_t>(year);
  *outMonth = static_cast<uint8_t>(month);
  *outDay = static_cast<uint8_t>(day);
  return true;
}

// Baut die JSON-Struktur (identisch fuer LittleFS-Persistenz und main/config/state).
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

  File file = configFs.open(kConfigPath, FILE_WRITE);
  if (!file) {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: Datei zum Schreiben nicht oeffenbar.");
    return;
  }
  serializeJson(doc, file);
  file.close();
  Logger::log(Logger::Type::INFO, Logger::Source::SYSTEM, "ConfigStore: config.json gespeichert.");
}

void load() {
  if (!configFs.exists(kConfigPath)) {
    return;  // keine gespeicherte Konfiguration -> Defaults bleiben aktiv
  }

  File file = configFs.open(kConfigPath, FILE_READ);
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

  File file = configFs.open(kProgramsPath, FILE_WRITE);
  if (!file) {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: programs.json nicht schreibbar.");
    return;
  }
  serializeJson(doc, file);
  file.close();
  Logger::log(Logger::Type::INFO, Logger::Source::SYSTEM, "ConfigStore: programs.json gespeichert.");
}

void loadProgramsFile() {
  programCount = 0;
  activeProgram = 0;
  if (!configFs.exists(kProgramsPath)) {
    return;  // keine gespeicherten Programme -> leere Liste bleibt aktiv
  }

  File file = configFs.open(kProgramsPath, FILE_READ);
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

// Baut die JSON-Struktur fuer schedule.json / main/schedule/state (identisch fuer beide).
void buildScheduleJson(JsonDocument &doc) {
  JsonArray arr = doc.createNestedArray("schedule");
  for (uint8_t i = 0; i < scheduleCount; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["enabled"] = schedule[i].enabled;
    obj["type"] = kScheduleTypeLabels[static_cast<uint8_t>(schedule[i].type)];

    // String-Werte muessen kopiert werden (Arduino String erzwingt bei ArduinoJson v6
    // eine Kopie in den Dokument-Pool) - anders als bei statischen Literalen (kWeekdayLabels)
    // waeren lokale char-Puffer hier ein Dangling-Pointer-Risiko, sobald die Schleife die
    // naechste Iteration erreicht (siehe Phase-14-Erfahrung mit main/programs/state).
    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", schedule[i].hour, schedule[i].minute);
    obj["time"] = String(timeBuf);

    if (schedule[i].type == ConfigStore::ScheduleType::WEEKLY) {
      JsonArray weekdaysArr = obj.createNestedArray("weekdays");
      for (uint8_t w = 0; w < 7; w++) {
        if (schedule[i].weekdaysMask & (1 << w)) {
          weekdaysArr.add(kWeekdayLabels[w]);
        }
      }
    }
    if (schedule[i].type == ConfigStore::ScheduleType::ONCE) {
      char dateBuf[11];
      snprintf(dateBuf, sizeof(dateBuf), "%04u-%02u-%02u", schedule[i].year, schedule[i].month, schedule[i].day);
      obj["date"] = String(dateBuf);
    }
    obj["program"] = schedule[i].program;
  }
  doc["enabled"] = scheduleGlobalEnabled;
}

void saveScheduleFile() {
  StaticJsonDocument<ConfigStore::kScheduleJsonCapacity> doc;
  buildScheduleJson(doc);

  File file = configFs.open(kSchedulePath, FILE_WRITE);
  if (!file) {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: schedule.json nicht schreibbar.");
    return;
  }
  serializeJson(doc, file);
  file.close();
  Logger::log(Logger::Type::INFO, Logger::Source::SYSTEM, "ConfigStore: schedule.json gespeichert.");
}

void loadScheduleFile() {
  scheduleCount = 0;
  scheduleGlobalEnabled = true;
  if (!configFs.exists(kSchedulePath)) {
    return;  // kein gespeicherter Zeitplan -> leere Liste bleibt aktiv
  }

  File file = configFs.open(kSchedulePath, FILE_READ);
  if (!file) {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: schedule.json nicht lesbar.");
    return;
  }

  StaticJsonDocument<ConfigStore::kScheduleJsonCapacity> doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: JSON-Fehler in schedule.json: %s",
                 err.c_str());
    return;
  }

  JsonArrayConst arr = doc["schedule"];
  uint8_t i = 0;
  for (JsonObjectConst obj : arr) {
    if (i >= ConfigStore::kMaxScheduleEntries) {
      break;
    }
    const char *program = obj["program"] | "";
    strncpy(schedule[i].program, program, ConfigStore::kAliasMaxLength);
    schedule[i].program[ConfigStore::kAliasMaxLength] = '\0';

    schedule[i].enabled = obj["enabled"] | true;

    ConfigStore::ScheduleType type = ConfigStore::ScheduleType::DAILY;
    if (!parseScheduleType(obj["type"] | "", &type)) {
      Logger::logf(Logger::Type::ERROR, Logger::Source::SYSTEM,
                   "ConfigStore: ungueltiger type in schedule.json Eintrag %u, uebersprungen.", i);
      continue;
    }
    schedule[i].type = type;

    uint8_t hour = 0;
    uint8_t minute = 0;
    if (!parseTimeString(obj["time"] | "", &hour, &minute)) {
      Logger::logf(Logger::Type::ERROR, Logger::Source::SYSTEM,
                   "ConfigStore: ungueltige time in schedule.json Eintrag %u, uebersprungen.", i);
      continue;
    }
    schedule[i].hour = hour;
    schedule[i].minute = minute;

    schedule[i].weekdaysMask = 0;
    if (type == ConfigStore::ScheduleType::WEEKLY) {
      JsonArrayConst weekdaysArr = obj["weekdays"];
      for (JsonVariantConst wd : weekdaysArr) {
        const uint8_t bit = parseWeekdayLabel(wd.as<const char *>());
        if (bit != 0xFF) {
          schedule[i].weekdaysMask |= (1 << bit);
        }
      }
    }

    schedule[i].year = 0;
    schedule[i].month = 0;
    schedule[i].day = 0;
    if (type == ConfigStore::ScheduleType::ONCE) {
      uint16_t year = 0;
      uint8_t month = 0;
      uint8_t day = 0;
      if (!parseDateString(obj["date"] | "", &year, &month, &day)) {
        Logger::logf(Logger::Type::ERROR, Logger::Source::SYSTEM,
                     "ConfigStore: ungueltiges date in schedule.json Eintrag %u, uebersprungen.", i);
        continue;
      }
      schedule[i].year = year;
      schedule[i].month = month;
      schedule[i].day = day;
    }

    i++;
  }
  scheduleCount = i;
  scheduleGlobalEnabled = doc["enabled"] | true;
}

}  // namespace

void ConfigStore::begin() {
  // Eigene Partition "config" (siehe partitions.csv) - komplett getrennt von "webfs",
  // die WebManager per `pio run --target uploadfs` bespielt. Dadurch kann ein Dashboard-
  // Update (uploadfs ueberschreibt immer die komplette Zielpartition) die persistierte
  // Konfiguration nicht mehr mitloeschen (siehe docs/spec/16-webif-fundament.md fuer den
  // Bug, der bei gemeinsamer Partition auftrat). Auto-Format hier bewusst wieder aktiv
  // (anders als seinerzeit bei der gemeinsamen Partition): "config" wird von uploadfs nie
  // beschrieben, ein Mount-Fehlschlag bedeutet hier also eine wirklich leere/neue
  // Partition, fuer die Auto-Format das korrekte, sichere Verhalten ist.
  if (!configFs.begin(true, "/config", 10, "config")) {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "ConfigStore: LittleFS-Mount fehlgeschlagen.");
    return;
  }
  load();
  loadProgramsFile();
  loadScheduleFile();
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

size_t ConfigStore::programsToJson(char *buffer, size_t bufferSize) {
  StaticJsonDocument<kProgramsJsonCapacity> doc;
  buildProgramsJson(doc);
  return serializeJson(doc, buffer, bufferSize);
}

uint8_t ConfigStore::getProgramIndexForName(const char *name) {
  if (name == nullptr) {
    return 0;
  }
  for (uint8_t p = 0; p < programCount; p++) {
    if (strcmp(programs[p].name, name) == 0) {
      return p + 1;
    }
  }
  return 0;
}

void ConfigStore::setSchedule(const ScheduleInput *entries, uint8_t count) {
  if (count > kMaxScheduleEntries) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::SYSTEM,
                 "ConfigStore: %u Zeitplan-Eintraege angefragt, nur %u erlaubt - Rest wird ignoriert.", count,
                 kMaxScheduleEntries);
    count = kMaxScheduleEntries;
  }
  for (uint8_t i = 0; i < count; i++) {
    strncpy(schedule[i].program, entries[i].program, kAliasMaxLength);
    schedule[i].program[kAliasMaxLength] = '\0';
    schedule[i].enabled = entries[i].enabled;
    schedule[i].type = entries[i].type;
    schedule[i].hour = entries[i].hour;
    schedule[i].minute = entries[i].minute;
    schedule[i].weekdaysMask = entries[i].weekdaysMask;
    schedule[i].year = entries[i].year;
    schedule[i].month = entries[i].month;
    schedule[i].day = entries[i].day;
  }
  scheduleCount = count;
  saveScheduleFile();
}

uint8_t ConfigStore::getScheduleCount() {
  return scheduleCount;
}

bool ConfigStore::getScheduleEnabled(uint8_t index) {
  if (index >= scheduleCount) {
    return false;
  }
  return schedule[index].enabled;
}

ConfigStore::ScheduleType ConfigStore::getScheduleType(uint8_t index) {
  if (index >= scheduleCount) {
    return ScheduleType::DAILY;
  }
  return schedule[index].type;
}

uint8_t ConfigStore::getScheduleHour(uint8_t index) {
  if (index >= scheduleCount) {
    return 0;
  }
  return schedule[index].hour;
}

uint8_t ConfigStore::getScheduleMinute(uint8_t index) {
  if (index >= scheduleCount) {
    return 0;
  }
  return schedule[index].minute;
}

uint8_t ConfigStore::getScheduleWeekdaysMask(uint8_t index) {
  if (index >= scheduleCount) {
    return 0;
  }
  return schedule[index].weekdaysMask;
}

uint16_t ConfigStore::getScheduleYear(uint8_t index) {
  if (index >= scheduleCount) {
    return 0;
  }
  return schedule[index].year;
}

uint8_t ConfigStore::getScheduleMonth(uint8_t index) {
  if (index >= scheduleCount) {
    return 0;
  }
  return schedule[index].month;
}

uint8_t ConfigStore::getScheduleDay(uint8_t index) {
  if (index >= scheduleCount) {
    return 0;
  }
  return schedule[index].day;
}

const char *ConfigStore::getScheduleProgram(uint8_t index) {
  if (index >= scheduleCount) {
    return "";
  }
  return schedule[index].program;
}

bool ConfigStore::getScheduleGlobalEnabled() {
  return scheduleGlobalEnabled;
}

void ConfigStore::setScheduleGlobalEnabled(bool enabled) {
  scheduleGlobalEnabled = enabled;
  saveScheduleFile();
}

size_t ConfigStore::scheduleToJson(char *buffer, size_t bufferSize) {
  StaticJsonDocument<kScheduleJsonCapacity> doc;
  buildScheduleJson(doc);
  return serializeJson(doc, buffer, bufferSize);
}
