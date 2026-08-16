#include "MqttManager.h"

#include <ArduinoJson.h>
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cstdlib>
#include <cstring>

#include "ConfigStore.h"
#include "Diagnostics.h"
#include "Logger.h"
#include "Sequencer.h"
#include "ValveController.h"
#include "ValveTimer.h"
#include "WifiManager.h"
#include "secrets.h"

namespace {

constexpr const char *kAvailabilityTopic = "gartenwasser/availability";
constexpr const char *kOnlinePayload = "online";
constexpr const char *kOfflinePayload = "offline";
constexpr uint8_t kAvailabilityQos = 1;
constexpr const char *kMaxTimeTopic = "gartenwasser/main/time/maxTime";
constexpr const char *kMainCmdTopic = "gartenwasser/main/cmd";
constexpr const char *kMainStateTopic = "gartenwasser/main/state";
constexpr const char *kMainActiveValveTopic = "gartenwasser/main/activeValve";
constexpr const char *kMainRemainingTotalTopic = "gartenwasser/main/remainingTotal";
constexpr const char *kI2cStatusTopic = "gartenwasser/diagnostics/i2cStatus";
constexpr const char *kLastErrorTopic = "gartenwasser/diagnostics/lastError";
// V0 hat keinen cmd/time/auto, aber einen Alias - eigenes Topic statt ueber
// parseValveTopic() (das ist bewusst auf V1..V5 begrenzt).
constexpr const char *kV0AliasSetTopic = "gartenwasser/V0/alias/set";
constexpr const char *kConfigSetTopic = "gartenwasser/main/config/set";
constexpr const char *kConfigStateTopic = "gartenwasser/main/config/state";
// Phase 14 (Bewaesserungsprogramme): main/program/cmd|state (Singular) = einfache
// Index-Auswahl, main/programs/set|state (Plural) = Bulk-JSON (Array-Replace + activeProgram),
// eigener Bereich getrennt von main/config/* (siehe docs/requirements.md, "Konfiguration").
constexpr const char *kProgramCmdTopic = "gartenwasser/main/program/cmd";
constexpr const char *kProgramStateTopic = "gartenwasser/main/program/state";
constexpr const char *kProgramsSetTopic = "gartenwasser/main/programs/set";
constexpr const char *kProgramsStateTopic = "gartenwasser/main/programs/state";

constexpr char kValvePrefix[] = "gartenwasser/V";
constexpr char kCmdSuffix[] = "/cmd";
constexpr char kTimeSetSuffix[] = "/time/set";
constexpr char kAutoSetSuffix[] = "/auto/set";
constexpr char kAliasSetSuffix[] = "/alias/set";

// Gemeinsame Puffergroesse fuer alle zusammengebauten Topic-Strings (z.B.
// "gartenwasser/V1/time/remaining"). Grosszuegig bemessen statt jede Topic-
// Variante einzeln zu vermessen - genau daraus resultierte einmal ein Bug
// (V{n}/auto/state wurde mit zu kleinem Puffer abgeschnitten, siehe Log.md).
constexpr size_t kTopicBufferSize = 48;

// Groesste vorkommende JSON-Payload (main/programs/set|state, Phase 14) - main/config/*
// bleibt kleiner (kJsonCapacity), programs.json ist mit bis zu kMaxPrograms Eintraegen
// der Puffer-bestimmende Fall.
constexpr size_t kMaxJsonPayloadSize = ConfigStore::kProgramsJsonCapacity;

// Grenzen fuer time/set (Minuten). Obere Grenze ist ein grosszuegiger Sanity-Check,
// die eigentliche Deckelung der effektiven Laufzeit erfolgt ueber maxTime (ValveTimer).
constexpr long kMinValveTimeMinutes = 1;
constexpr long kMaxValveTimeMinutes = 999;

// Sekuendlicher Countdown-Tick ist sicherheitskritisch (Ventil-Abschaltung bei
// Zeitablauf) und muss unabhaengig von WLAN/MQTT laufen, siehe docs/requirements.md
// ("laeuft lokal/autonom weiter").
constexpr unsigned long kTickIntervalMs = 1000;

// Analog zum Reconnect-Intervall aus WifiManager: kurz genug fuer zuegige
// Wiederholung, lang genug, um den Broker nicht mit Verbindungsversuchen zu fluten.
constexpr unsigned long kReconnectIntervalMs = 15000;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Sorgt dafuer, dass der erste Verbindungsversuch sofort erfolgt (nicht erst
// 15s nach Boot) - der Reconnect-Intervall-Check greift sonst auch beim allerersten Versuch.
unsigned long lastAttemptMs = 0 - kReconnectIntervalMs;
unsigned long lastTickMs = 0;
bool wasConnected = false;

// Erkennt Topics der Form "gartenwasser/V{1..5}<suffix>" und liefert den Ventil-Index.
bool parseValveTopic(const char *topic, const char *suffix, uint8_t *outIndex) {
  const size_t prefixLen = strlen(kValvePrefix);
  if (strncmp(topic, kValvePrefix, prefixLen) != 0) {
    return false;
  }
  const char *rest = topic + prefixLen;
  if (rest[0] < '1' || rest[0] > '5') {
    return false;
  }
  if (strcmp(rest + 1, suffix) != 0) {
    return false;
  }
  *outIndex = static_cast<uint8_t>(rest[0] - '0');
  return true;
}

void copyPayload(const uint8_t *payload, unsigned int length, char *outStr, size_t outSize) {
  const unsigned int copyLen = length < outSize - 1 ? length : outSize - 1;
  memcpy(outStr, payload, copyLen);
  outStr[copyLen] = '\0';
}

bool parseOnOffPayload(const char *payloadStr, bool *outOn) {
  if (strcmp(payloadStr, "ON") == 0) {
    *outOn = true;
    return true;
  }
  if (strcmp(payloadStr, "OFF") == 0) {
    *outOn = false;
    return true;
  }
  return false;
}

// Zentrale Publish-Stelle: sendet und loggt jedes ausgehende MQTT-Ereignis (Type::PUB).
void publishAndLog(const char *topic, const char *payload, bool retained) {
  mqttClient.publish(topic, payload, retained);
  Logger::logf(Logger::Type::PUB, Logger::Source::MQTT, "%s = %s", topic, payload);
}

void publishValveState(uint8_t index, bool on) {
  char topic[kTopicBufferSize];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/state", index);
  publishAndLog(topic, on ? "ON" : "OFF", true);
}

void publishTimeState(uint8_t index, uint16_t minutes) {
  char topic[kTopicBufferSize];
  char payload[8];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/time/state", index);
  snprintf(payload, sizeof(payload), "%u", minutes);
  publishAndLog(topic, payload, true);
}

void publishRemaining(uint8_t index, uint16_t seconds) {
  char topic[kTopicBufferSize];
  char payload[8];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/time/remaining", index);
  snprintf(payload, sizeof(payload), "%02u:%02u", seconds / 60, seconds % 60);
  publishAndLog(topic, payload, false);  // nicht retained (sekuendlicher Live-Wert)
}

void publishMaxTime() {
  char payload[8];
  snprintf(payload, sizeof(payload), "%u", ConfigStore::getMaxTime());
  publishAndLog(kMaxTimeTopic, payload, true);
}

void publishAutoState(uint8_t index, bool on) {
  char topic[kTopicBufferSize];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/auto/state", index);
  publishAndLog(topic, on ? "ON" : "OFF", true);
}

void publishAlias(uint8_t index, const char *alias) {
  char topic[kTopicBufferSize];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/alias", index);
  publishAndLog(topic, alias, true);
}

// Laenge (siehe ConfigStore::kAliasMaxLength) und Steuerzeichen pruefen. UTF-8-
// Mehrbyte-Folgen (Umlaute etc., Bytes >= 0x80) sind ausdruecklich erlaubt.
bool isValidAliasPayload(const char *payloadStr, size_t length) {
  if (length > ConfigStore::kAliasMaxLength) {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    if (static_cast<unsigned char>(payloadStr[i]) < 0x20) {
      return false;
    }
  }
  return true;
}

// Publiziert die komplette aktuelle Konfiguration (time/auto/alias/maxTime)
// als JSON, retained - bei jeder Aenderung (egal welches Topic) und nach
// jedem (Re-)Connect (siehe docs/spec/11-sammelbefehle.md).
void publishConfigState() {
  char payload[ConfigStore::kJsonCapacity];
  const size_t written = ConfigStore::toJson(payload, sizeof(payload));
  if (written == 0) {
    Logger::log(Logger::Type::ERROR, Logger::Source::MQTT, "main/config/state: Serialisierung fehlgeschlagen.");
    return;
  }
  publishAndLog(kConfigStateTopic, payload, true);
}

// Publiziert das aktuell gewaehlte Programm (Singular, kompakt), retained -
// bei jeder Aenderung und nach jedem (Re-)Connect (siehe docs/spec/14-programme.md).
void publishProgramState() {
  const uint8_t active = ConfigStore::getActiveProgram();
  StaticJsonDocument<96> doc;
  doc["index"] = active;
  if (active == 0 || active > ConfigStore::getProgramCount()) {
    doc["name"] = nullptr;
  } else {
    doc["name"] = ConfigStore::getProgramName(active);
  }
  char payload[96];
  serializeJson(doc, payload, sizeof(payload));
  publishAndLog(kProgramStateTopic, payload, true);
}

// Publiziert die komplette Programme-Liste + activeProgram als JSON, retained -
// bei jeder Aenderung (egal welches Topic) und nach jedem (Re-)Connect.
void publishProgramsState() {
  char payload[ConfigStore::kProgramsJsonCapacity];
  const size_t written = ConfigStore::programsToJson(payload, sizeof(payload));
  if (written == 0) {
    Logger::log(Logger::Type::ERROR, Logger::Source::MQTT, "main/programs/state: Serialisierung fehlgeschlagen.");
    return;
  }
  publishAndLog(kProgramsStateTopic, payload, true);
}

void publishMainState(bool running) {
  publishAndLog(kMainStateTopic, running ? "ON" : "OFF", true);
}

void publishI2cStatus(bool ok) {
  publishAndLog(kI2cStatusTopic, ok ? "ok" : "error", true);
}

void publishLastError(const char *text) {
  publishAndLog(kLastErrorTopic, text, true);
}

// Sicherheitskritisch im weiteren Sinne (Fehlererkennung soll auch ohne MQTT
// funktionieren) - laeuft daher wie tickValveTimers() unabhaengig von WLAN/MQTT.
void checkDiagnostics() {
  if (Diagnostics::checkI2cStatus()) {
    publishI2cStatus(Diagnostics::isI2cOk());
  }
  if (Diagnostics::consumeNewError()) {
    publishLastError(Diagnostics::getLastError());
  }
}

void publishActiveValve(uint8_t index) {
  char payload[4];
  if (index == 0) {
    snprintf(payload, sizeof(payload), "-");
  } else {
    snprintf(payload, sizeof(payload), "V%u", index);
  }
  publishAndLog(kMainActiveValveTopic, payload, true);
}

// Restzeit der Gesamtsequenz: Restlaufzeit des aktiven Ventils + effektive
// Laufzeit (min(time, maxTime)) aller noch ausstehenden Ventile in der Warteschlange.
uint32_t computeRemainingTotalSeconds() {
  if (!Sequencer::isRunning()) {
    return 0;
  }
  uint32_t total = ValveTimer::getRemainingSeconds(Sequencer::getActiveValve());
  const uint8_t pendingCount = Sequencer::getPendingCount();
  for (uint8_t i = 0; i < pendingCount; i++) {
    const uint8_t v = Sequencer::getPendingValve(i);
    const uint16_t timeMinutes = ConfigStore::getValveTime(v);
    const uint16_t maxTimeMinutes = ConfigStore::getMaxTime();
    const uint16_t effectiveMinutes = timeMinutes < maxTimeMinutes ? timeMinutes : maxTimeMinutes;
    total += static_cast<uint32_t>(effectiveMinutes) * 60UL;
  }
  return total;
}

void publishRemainingTotalNow() {
  const uint32_t seconds = computeRemainingTotalSeconds();
  char payload[12];
  snprintf(payload, sizeof(payload), "%02lu:%02lu", seconds / 60, seconds % 60);
  publishAndLog(kMainRemainingTotalTopic, payload, false);  // nicht retained (sekuendlicher Live-Wert)
}

// V0-Kopplung: V1-V5 ON schaltet V0 mit ein; V1-V5 OFF schaltet V0 nur aus,
// wenn kein anderes Bewaesserungsventil mehr aktiv ist (siehe docs/requirements.md).
// Steuert ausserdem den Laufzeit-Countdown (ValveTimer) mit.
void applyValveCommand(uint8_t index, bool targetOn) {
  if (ValveController::getValve(index) == targetOn) {
    return;  // keine Aenderung, kein State-Update noetig
  }

  ValveController::setValve(index, targetOn);
  publishValveState(index, targetOn);

  if (targetOn) {
    ValveTimer::start(index);
    publishRemaining(index, ValveTimer::getRemainingSeconds(index));
    if (!ValveController::getValve(0)) {
      ValveController::setValve(0, true);
      publishValveState(0, true);
    }
  } else {
    // Restlaufzeit-Anzeige auf 0: ob wieder armiert wird (auf `time` zurueck) oder
    // nicht, entscheidet der Aufrufer - haengt davon ab, ob eine Automatik-Sequenz
    // damit weitermacht (dann bleibt 0, siehe advanceSequence()) oder nicht
    // (dann sofort re-armieren, siehe armIdleValve()).
    ValveTimer::clear(index);
    publishRemaining(index, 0);
    if (!ValveController::anyIrrigationValveActive() && ValveController::getValve(0)) {
      ValveController::setValve(0, false);
      publishValveState(0, false);
    }
  }
}

// Armiert ein einzelnes idle Ventil wieder auf seine konfigurierte effektive
// Laufzeit und publiziert die aktualisierte Restlaufzeit.
void armIdleValve(uint8_t index) {
  ValveTimer::reset(index);
  publishRemaining(index, ValveTimer::getRemainingSeconds(index));
}

// Alle Ventile wieder armieren - nach Ende einer Automatik-Sequenz (natuerlich
// oder per main/cmd OFF), siehe docs/requirements.md ("alle Timer auf time zurueck").
void armAllValves() {
  for (uint8_t i = 1; i <= 5; i++) {
    armIdleValve(i);
  }
}

// Ruecken der Automatik-Sequenz zum naechsten Ventil vor (nach Zeitablauf ODER
// manuellem OFF des gerade aktiven Ventils - beides identisch behandelt, siehe
// docs/spec/07-automatik-sequenz.md). Schaltet das naechste Ventil ein bzw.
// beendet die Sequenz, wenn die Warteschlange erschoepft ist.
void advanceSequence() {
  const uint8_t next = Sequencer::advance();
  if (next == 0) {
    publishMainState(false);
    publishActiveValve(0);
    armAllValves();  // Sequenz fertig: alle Restlaufzeiten zurueck auf `time`
  } else {
    applyValveCommand(next, true);
    publishActiveValve(next);
  }
  publishRemainingTotalNow();
}

// Vorwaertsdeklaration: applyProgram() ist erst weiter unten definiert, wird aber
// bereits hier in startSequence() benoetigt (Automatik ohne gewaehltes Programm
// ergibt keinen Sinn mehr, seit es Programme gibt - siehe docs/spec/14-programme.md).
void applyProgram(uint8_t programIndex);

void startSequence() {
  if (Sequencer::isRunning()) {
    Logger::log(Logger::Type::INFO, Logger::Source::MQTT, "main/cmd ON ignoriert (Automatik laeuft bereits).");
    return;
  }

  const uint8_t activeProgram = ConfigStore::getActiveProgram();
  if (activeProgram == 0) {
    Logger::log(Logger::Type::ERROR, Logger::Source::MQTT, "main/cmd ON ignoriert: kein Programm gewaehlt.");
    return;
  }
  // Ventile exakt auf den aktuellen Stand des gewaehlten Programms bringen, bevor die
  // Sequenz startet - verhindert Drift durch zwischenzeitliche manuelle Aenderungen.
  applyProgram(activeProgram);

  uint8_t autoValves[5];
  uint8_t count = 0;
  for (uint8_t i = 1; i <= 5; i++) {
    if (ValveController::getAuto(i)) {
      autoValves[count++] = i;
    }
  }

  if (!Sequencer::start(autoValves, count)) {
    Logger::log(Logger::Type::ERROR, Logger::Source::MQTT, "main/cmd ON ignoriert: kein Ventil mit auto=ON.");
    return;
  }

  publishMainState(true);
  const uint8_t first = Sequencer::getActiveValve();
  applyValveCommand(first, true);
  publishActiveValve(first);
  publishRemainingTotalNow();
}

void stopSequence() {
  const bool wasRunning = Sequencer::isRunning();
  const uint8_t active = Sequencer::getActiveValve();
  if (active != 0) {
    applyValveCommand(active, false);
  }
  Sequencer::stop();
  publishMainState(false);
  publishActiveValve(0);
  if (wasRunning) {
    armAllValves();  // Sequenz abgebrochen: alle Restlaufzeiten zurueck auf `time`
  }
  publishRemainingTotalNow();
}

void handleMainCmd(const char *payloadStr) {
  bool targetOn = false;
  if (!parseOnOffPayload(payloadStr, &targetOn)) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Payload '%s' fuer main/cmd", payloadStr);
    return;
  }
  if (targetOn) {
    startSequence();
  } else {
    stopSequence();
  }
}

void handleValveCmd(uint8_t index, const char *payloadStr) {
  bool targetOn = false;
  if (!parseOnOffPayload(payloadStr, &targetOn)) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Payload '%s' fuer V%u/cmd", payloadStr,
                 index);
    return;
  }

  // Waehrend die Automatik laeuft: manuelles ON ignorieren (siehe
  // docs/spec/07-automatik-sequenz.md); manuelles OFF des aktiven Ventils
  // wird angenommen und stoesst den naechsten Schritt der Sequenz an.
  if (targetOn && Sequencer::isRunning()) {
    Logger::logf(Logger::Type::INFO, Logger::Source::MQTT, "V%u/cmd ON ignoriert (Automatik laeuft).", index);
    return;
  }

  const bool wasActiveSequenceValve = Sequencer::isRunning() && Sequencer::getActiveValve() == index;
  applyValveCommand(index, targetOn);
  if (!targetOn) {
    if (wasActiveSequenceValve) {
      advanceSequence();  // Restlaufzeit bleibt 0, bis die ganze Sequenz endet
    } else {
      armIdleValve(index);  // normaler manueller Stopp: sofort wieder auf `time` armiert
    }
  }
}

// Kernlogik ohne Payload-Parsing, wiederverwendet von main/config/set
// (dort kommt der Wert direkt als JSON-Bool statt als ON/OFF-String).
void applyAutoValue(uint8_t index, bool on) {
  ValveController::setAuto(index, on);
  publishAutoState(index, on);
}

void handleAutoSet(uint8_t index, const char *payloadStr) {
  bool targetOn = false;
  if (!parseOnOffPayload(payloadStr, &targetOn)) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Payload '%s' fuer V%u/auto/set", payloadStr,
                 index);
    return;
  }
  applyAutoValue(index, targetOn);
  publishConfigState();
}

// Kernlogik ohne Payload-Parsing, wiederverwendet von main/config/set
// (dort kommt der Wert direkt als JSON-Zahl statt als String).
void applyTimeValue(uint8_t index, long value) {
  if (value < kMinValveTimeMinutes || value > kMaxValveTimeMinutes) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Wert '%ld' fuer V%u/time", value, index);
    return;
  }

  const uint16_t minutes = static_cast<uint16_t>(value);
  ConfigStore::setValveTime(index, minutes);
  publishTimeState(index, minutes);

  // Ventil laeuft gerade nicht und es laeuft auch keine Automatik-Sequenz:
  // angezeigte Restlaufzeit sofort an die neue Konfiguration anpassen (armiert),
  // statt auf den naechsten Start zu warten. Waehrend einer laufenden Sequenz
  // NICHT re-armieren - sonst sieht ein bereits durchgelaufenes Ventil so aus,
  // als waere es noch an der Reihe (siehe armIdleValve()/advanceSequence()).
  if (!ValveController::getValve(index) && !Sequencer::isRunning()) {
    armIdleValve(index);
  }
}

void handleTimeSet(uint8_t index, const char *payloadStr) {
  char *endPtr = nullptr;
  const long value = strtol(payloadStr, &endPtr, 10);
  if (endPtr == payloadStr || *endPtr != '\0') {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Wert '%s' fuer V%u/time/set", payloadStr,
                 index);
    return;
  }
  applyTimeValue(index, value);
  publishConfigState();
}

// Kernlogik ohne Aufrufkontext, wiederverwendet von main/config/set.
void applyAliasValue(uint8_t index, const char *alias, unsigned int length) {
  if (!isValidAliasPayload(alias, length)) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Alias '%s' fuer V%u", alias, index);
    return;
  }
  ValveController::setAlias(index, alias);
  publishAlias(index, alias);
}

void handleAliasSet(uint8_t index, const char *payloadStr, unsigned int length) {
  applyAliasValue(index, payloadStr, length);
  publishConfigState();
}

// Kernlogik fuer maxTime - bisher nur ueber main/config/set erreichbar
// (kein eigenes main/time/set-Topic, siehe docs/spec/11-sammelbefehle.md).
void applyMaxTimeValue(long value) {
  if (value < kMinValveTimeMinutes || value > kMaxValveTimeMinutes) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/config/set: ungueltiger maxTime-Wert '%ld'", value);
    return;
  }
  ConfigStore::setMaxTime(static_cast<uint16_t>(value));
  publishMaxTime();
}

// Erkennt Keys der Form "V<digit>" (z.B. "V1") und liefert den Index, sofern
// er in [minIndex, maxIndex] liegt. Unbekannte/ausserhalb liegende Keys (z.B.
// "V9") liefern false, damit der Aufrufer sie ignorieren + loggen kann.
bool parseValveKey(const char *key, uint8_t minIndex, uint8_t maxIndex, uint8_t *outIndex) {
  if (key[0] != 'V' || key[1] < '0' || key[1] > '9' || key[2] != '\0') {
    return false;
  }
  const uint8_t index = static_cast<uint8_t>(key[1] - '0');
  if (index < minIndex || index > maxIndex) {
    return false;
  }
  *outIndex = index;
  return true;
}

void handleConfigSet(const char *payloadStr) {
  StaticJsonDocument<ConfigStore::kJsonCapacity> doc;
  const DeserializationError err = deserializeJson(doc, payloadStr);
  if (err) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/config/set: JSON-Fehler: %s", err.c_str());
    return;
  }

  if (!doc["maxTime"].isNull()) {
    applyMaxTimeValue(doc["maxTime"].as<long>());
  }

  uint8_t index = 0;
  JsonObjectConst timeObj = doc["time"];
  for (JsonPairConst kv : timeObj) {
    if (parseValveKey(kv.key().c_str(), 1, 5, &index)) {
      applyTimeValue(index, kv.value().as<long>());
    } else {
      Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/config/set: unbekanntes Ventil '%s' in time",
                   kv.key().c_str());
    }
  }

  JsonObjectConst autoObj = doc["auto"];
  for (JsonPairConst kv : autoObj) {
    if (parseValveKey(kv.key().c_str(), 1, 5, &index)) {
      applyAutoValue(index, kv.value().as<bool>());
    } else {
      Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/config/set: unbekanntes Ventil '%s' in auto",
                   kv.key().c_str());
    }
  }

  JsonObjectConst aliasObj = doc["alias"];
  for (JsonPairConst kv : aliasObj) {
    if (parseValveKey(kv.key().c_str(), 0, 5, &index)) {
      const char *aliasValue = kv.value().as<const char *>();
      applyAliasValue(index, aliasValue, strlen(aliasValue));
    } else {
      Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/config/set: unbekanntes Ventil '%s' in alias",
                   kv.key().c_str());
    }
  }

  publishConfigState();
}

// Kernfunktion (Phase 14): wendet Programm `programIndex` (1-basiert) an, indem fuer jedes im
// Programm enthaltene Feld dieselben applyTimeValue()/applyAutoValue()-Kernfunktionen wie
// main/config/set aufgerufen werden (siehe docs/spec/14-programme.md, Kernentscheidung 3).
// `programIndex == 0` loescht nur die Auswahl, ohne Ventile anzufassen. Wiederverwendet von
// main/program/cmd und dem Key "activeProgram" in main/programs/set.
void applyProgram(uint8_t programIndex) {
  if (programIndex != 0) {
    if (programIndex > ConfigStore::getProgramCount()) {
      Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Programm-Index '%u'", programIndex);
      return;
    }
    for (uint8_t v = 1; v <= 5; v++) {
      if (ConfigStore::programHasTime(programIndex, v)) {
        applyTimeValue(v, ConfigStore::getProgramTime(programIndex, v));
      }
      if (ConfigStore::programHasAuto(programIndex, v)) {
        applyAutoValue(v, ConfigStore::getProgramAuto(programIndex, v));
      }
    }
  }
  ConfigStore::setActiveProgram(programIndex);
  publishProgramState();
  publishProgramsState();
  publishConfigState();  // time/auto koennen sich durchs Anwenden geaendert haben
}

void handleProgramCmd(const char *payloadStr) {
  char *endPtr = nullptr;
  const long value = strtol(payloadStr, &endPtr, 10);
  if (endPtr == payloadStr || *endPtr != '\0' || value < 0 || value > ConfigStore::kMaxPrograms) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Wert '%s' fuer main/program/cmd",
                 payloadStr);
    return;
  }
  applyProgram(static_cast<uint8_t>(value));
}

// Wandelt "P1".."P4" in 1..4 um, alles andere (falscher String, fehlend) liefert 0
// (kein Shortcut) - wird nicht abgelehnt, siehe docs/spec/14-programme.md, Kernentscheidung 8.
uint8_t parseProgramShortcut(const char *value) {
  if (value == nullptr || value[0] != 'P' || value[1] < '1' || value[1] > '4' || value[2] != '\0') {
    return 0;
  }
  return static_cast<uint8_t>(value[1] - '0');
}

// main/programs/set: "programs" ersetzt das komplette Array (kein Feld-Merge einzelner
// Programme, siehe docs/spec/14-programme.md, Kernentscheidung 5), "activeProgram" wendet
// die Auswahl an (identisch zu main/program/cmd, ueber applyProgram()). Beide optional.
void handleProgramsSet(const char *payloadStr) {
  StaticJsonDocument<ConfigStore::kProgramsJsonCapacity> doc;
  const DeserializationError err = deserializeJson(doc, payloadStr);
  if (err) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/programs/set: JSON-Fehler: %s", err.c_str());
    return;
  }

  JsonArrayConst programsArr = doc["programs"];
  if (!programsArr.isNull()) {
    ConfigStore::ProgramInput entries[ConfigStore::kMaxPrograms];
    uint8_t count = 0;
    for (JsonObjectConst obj : programsArr) {
      if (count >= ConfigStore::kMaxPrograms) {
        Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT,
                     "main/programs/set: mehr als %u Programme, Rest wird ignoriert.", ConfigStore::kMaxPrograms);
        break;
      }
      ConfigStore::ProgramInput &entry = entries[count];
      entry.name = obj["name"] | "";
      entry.shortcut = parseProgramShortcut(obj["shortcut"] | "");
      for (uint8_t i = 0; i < 6; i++) {
        entry.timeSet[i] = false;
        entry.autoSet[i] = false;
      }
      JsonObjectConst timeObj = obj["time"];
      uint8_t idx = 0;
      for (JsonPairConst kv : timeObj) {
        if (parseValveKey(kv.key().c_str(), 1, 5, &idx)) {
          entry.time[idx] = kv.value().as<uint16_t>();
          entry.timeSet[idx] = true;
        } else {
          Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT,
                       "main/programs/set: unbekanntes Ventil '%s' in Programm-time", kv.key().c_str());
        }
      }
      JsonObjectConst autoObj = obj["auto"];
      for (JsonPairConst kv : autoObj) {
        if (parseValveKey(kv.key().c_str(), 1, 5, &idx)) {
          entry.autoFlag[idx] = kv.value().as<bool>();
          entry.autoSet[idx] = true;
        } else {
          Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT,
                       "main/programs/set: unbekanntes Ventil '%s' in Programm-auto", kv.key().c_str());
        }
      }
      count++;
    }
    ConfigStore::setPrograms(entries, count);
  }

  if (!doc["activeProgram"].isNull()) {
    const long value = doc["activeProgram"].as<long>();
    if (value < 0 || value > ConfigStore::kMaxPrograms) {
      Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/programs/set: ungueltiger activeProgram-Wert '%ld'",
                   value);
    } else {
      applyProgram(static_cast<uint8_t>(value));  // publiziert programs/program/config-State bereits
      return;
    }
  }

  publishProgramsState();
}

void handleMqttMessage(char *topic, uint8_t *payload, unsigned int length) {
  char payloadStr[kMaxJsonPayloadSize];
  copyPayload(payload, length, payloadStr, sizeof(payloadStr));
  Logger::logf(Logger::Type::SUB, Logger::Source::MQTT, "%s = %s", topic, payloadStr);

  if (strcmp(topic, kMainCmdTopic) == 0) {
    handleMainCmd(payloadStr);
    return;
  }
  if (strcmp(topic, kV0AliasSetTopic) == 0) {
    handleAliasSet(0, payloadStr, length);
    return;
  }
  if (strcmp(topic, kConfigSetTopic) == 0) {
    handleConfigSet(payloadStr);
    return;
  }
  if (strcmp(topic, kProgramCmdTopic) == 0) {
    handleProgramCmd(payloadStr);
    return;
  }
  if (strcmp(topic, kProgramsSetTopic) == 0) {
    handleProgramsSet(payloadStr);
    return;
  }

  uint8_t valveIndex = 0;
  if (parseValveTopic(topic, kCmdSuffix, &valveIndex)) {
    handleValveCmd(valveIndex, payloadStr);
  } else if (parseValveTopic(topic, kTimeSetSuffix, &valveIndex)) {
    handleTimeSet(valveIndex, payloadStr);
  } else if (parseValveTopic(topic, kAutoSetSuffix, &valveIndex)) {
    handleAutoSet(valveIndex, payloadStr);
  } else if (parseValveTopic(topic, kAliasSetSuffix, &valveIndex)) {
    handleAliasSet(valveIndex, payloadStr, length);
  } else {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Unbekanntes Topic: '%s'", topic);
  }
}

// Sicherheitskritisch: laeuft unabhaengig von WLAN/MQTT-Verbindungsstatus (siehe
// Aufrufstelle in MqttManager::loop()). publish()-Aufrufe sind bei fehlender
// Verbindung ein sicherer No-Op (PubSubClient liefert dann nur false zurueck).
void tickValveTimers() {
  uint8_t activeMask = 0;
  for (uint8_t i = 1; i <= 5; i++) {
    if (ValveController::getValve(i)) {
      activeMask |= (1 << i);
    }
  }

  const uint8_t expiredMask = ValveTimer::tick(activeMask);
  for (uint8_t i = 1; i <= 5; i++) {
    if (ValveController::getValve(i)) {
      publishRemaining(i, ValveTimer::getRemainingSeconds(i));
    }
    if (expiredMask & (1 << i)) {
      // Zeitablauf des aktiven Sequenz-Ventils wird identisch zu manuellem OFF
      // behandelt: naechstes Ventil der Automatik-Sequenz uebernehmen.
      const bool wasActiveSequenceValve = Sequencer::isRunning() && Sequencer::getActiveValve() == i;
      applyValveCommand(i, false);
      if (wasActiveSequenceValve) {
        advanceSequence();  // Restlaufzeit bleibt 0, bis die ganze Sequenz endet
      } else {
        armIdleValve(i);  // normaler Zeitablauf ausserhalb einer Sequenz: sofort re-armiert
      }
    }
  }
  if (Sequencer::isRunning()) {
    publishRemainingTotalNow();
  }
}

bool connectToBroker() {
  const bool ok = mqttClient.connect(MQTT_CLIENT_ID, kAvailabilityTopic, kAvailabilityQos, true, kOfflinePayload);
  if (ok) {
    Logger::log(Logger::Type::INFO, Logger::Source::MQTT, "Verbunden.");
    publishAndLog(kAvailabilityTopic, kOnlinePayload, true);
    mqttClient.subscribe(kMainCmdTopic);
    mqttClient.subscribe(kV0AliasSetTopic);
    mqttClient.subscribe(kConfigSetTopic);
    mqttClient.subscribe(kProgramCmdTopic);
    mqttClient.subscribe(kProgramsSetTopic);
    for (uint8_t i = 1; i <= 5; i++) {
      char cmdTopic[kTopicBufferSize];
      char timeSetTopic[kTopicBufferSize];
      char autoSetTopic[kTopicBufferSize];
      char aliasSetTopic[kTopicBufferSize];
      snprintf(cmdTopic, sizeof(cmdTopic), "gartenwasser/V%u/cmd", i);
      snprintf(timeSetTopic, sizeof(timeSetTopic), "gartenwasser/V%u/time/set", i);
      snprintf(autoSetTopic, sizeof(autoSetTopic), "gartenwasser/V%u/auto/set", i);
      snprintf(aliasSetTopic, sizeof(aliasSetTopic), "gartenwasser/V%u/alias/set", i);
      mqttClient.subscribe(cmdTopic);
      mqttClient.subscribe(timeSetTopic);
      mqttClient.subscribe(autoSetTopic);
      mqttClient.subscribe(aliasSetTopic);
    }
    for (uint8_t i = 0; i < ValveController::kValveCount; i++) {
      publishValveState(i, ValveController::getValve(i));
    }
    publishAlias(0, ValveController::getAlias(0));
    for (uint8_t i = 1; i <= 5; i++) {
      publishTimeState(i, ConfigStore::getValveTime(i));
      publishRemaining(i, ValveTimer::getRemainingSeconds(i));
      publishAutoState(i, ValveController::getAuto(i));
      publishAlias(i, ValveController::getAlias(i));
    }
    publishMaxTime();
    publishMainState(Sequencer::isRunning());
    publishActiveValve(Sequencer::getActiveValve());
    publishRemainingTotalNow();
    publishI2cStatus(Diagnostics::isI2cOk());
    if (Diagnostics::getLastError()[0] != '\0') {
      publishLastError(Diagnostics::getLastError());
    }
    publishConfigState();
    publishProgramState();
    publishProgramsState();
  }
  return ok;
}

}  // namespace

void MqttManager::begin() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(handleMqttMessage);
  // Default (256 Byte) reicht nicht fuer die JSON-Topics (main/config/*, main/programs/*).
  // main/programs/set|state (Phase 14, bis zu kMaxPrograms Eintraege) ist der groesste Fall.
  mqttClient.setBufferSize(static_cast<uint16_t>(kMaxJsonPayloadSize));
}

void MqttManager::loop() {
  const unsigned long now = millis();
  if (now - lastTickMs >= kTickIntervalMs) {
    lastTickMs = now;
    tickValveTimers();
    checkDiagnostics();
  }

  if (!WifiManager::isConnected()) {
    // Ohne WLAN ist ein MQTT-Reconnect-Versuch sinnlos; Status als getrennt fuehren.
    wasConnected = false;
    return;
  }

  mqttClient.loop();
  const bool connected = mqttClient.connected();

  // "Verbunden." wird direkt in connectToBroker() geloggt (vor den Publishes),
  // hier nur noch die Verbindungsverlust-Erkennung.
  if (!connected && wasConnected) {
    Logger::log(Logger::Type::ERROR, Logger::Source::MQTT, "MQTT Verbindung verloren.");
  }
  wasConnected = connected;

  if (!connected) {
    if (now - lastAttemptMs >= kReconnectIntervalMs) {
      lastAttemptMs = now;
      Logger::logf(Logger::Type::INFO, Logger::Source::MQTT, "Verbinde mit '%s:%d' ...", MQTT_BROKER, MQTT_PORT);
      if (!connectToBroker()) {
        Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Verbindung fehlgeschlagen (state=%d).",
                     mqttClient.state());
      }
    }
  }
}

bool MqttManager::isConnected() {
  return mqttClient.connected();
}

void MqttManager::requestMainCmd(bool on) {
  if (on) {
    startSequence();
  } else {
    stopSequence();
  }
}

void MqttManager::requestProgramByShortcut(uint8_t shortcut) {
  const uint8_t programIndex = ConfigStore::getProgramIndexForShortcut(shortcut);
  if (programIndex == 0) {
    Logger::logf(Logger::Type::INFO, Logger::Source::MQTT, "P%u: kein Programm mit diesem Shortcut hinterlegt.",
                 shortcut);
    return;
  }
  applyProgram(programIndex);
}

void MqttManager::requestProgramClear() {
  applyProgram(0);
}
