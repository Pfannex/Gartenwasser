#include "MQTT.h"

#include <ArduinoJson.h>
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "FileSystem.h"
#include "Diagnostics.h"
#include "Logger.h"
#include "AutomaticController.h"
#include "ValveController.h"
#include "ValveTimer.h"
#include "Version.h"
#include "BuildNumber.h"
#include "WiFiController.h"
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
// Firmware-Version (siehe include/Version.h) - retained, damit sich nach einem OTA-Update
// (Phase 21) im Web-Dashboard bestaetigen laesst, dass die neue Firmware tatsaechlich laeuft.
constexpr const char *kVersionTopic = "gartenwasser/diagnostics/version";
// Hardware-Status RAM/Flash (Merker aus Phase 21) - RAM aendert sich zur Laufzeit (Heap),
// daher periodisch aktualisiert (siehe checkHardwareStatus()); Flash (Sketch-Groesse) aendert
// sich nur mit einem neuen Firmware-Flash, wird aber der Einfachheit halber im selben Rhythmus
// mitpubliziert (retained, kein spuerbarer Mehraufwand).
constexpr const char *kRamTopic = "gartenwasser/diagnostics/ram";
constexpr const char *kFlashTopic = "gartenwasser/diagnostics/flash";
constexpr unsigned long kHardwareStatusIntervalMs = 30000;
// Live-Log (Nachtrag 2026-08-18): jede Logger-Zeile ausser PUB/SUB, siehe
// Logger::setLineCallback(). Bewusst nicht retained (reiner Live-Stream, kein Verlauf
// beim Verbinden).
constexpr const char *kLiveLogTopic = "gartenwasser/diagnostics/livelog";
// Anfrage-Topic: beliebiger Payload loest replayLogBuffer() aus - fuer Web-Clients, die die
// Log-Seite oeffnen und sonst (kein Retain) nur ab diesem Zeitpunkt neue Zeilen saehen.
constexpr const char *kLiveLogReplayTopic = "gartenwasser/diagnostics/livelog/replay";
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
// Phase 15 (Zeitplan): main/schedule/set|state = Bulk-JSON (Array-Replace + globales
// enabled), main/schedule/cmd = Convenience-Singular-Topic fuer den globalen Schalter
// (ON/OFF, analog main/program/cmd), main/schedule/cleanup = Einmalbefehl zum Entfernen
// abgelaufener "once"-Eintraege (siehe docs/spec/15-wochenplan.md).
constexpr const char *kScheduleSetTopic = "gartenwasser/main/schedule/set";
constexpr const char *kScheduleStateTopic = "gartenwasser/main/schedule/state";
constexpr const char *kScheduleCmdTopic = "gartenwasser/main/schedule/cmd";
constexpr const char *kScheduleCleanupTopic = "gartenwasser/main/schedule/cleanup";

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

// Groesste vorkommende JSON-Payload - main/config/* bleibt am kleinsten (kJsonCapacity),
// schedule.json ist mit bis zu kMaxScheduleEntries Eintraegen (Phase 15) inzwischen der
// Puffer-bestimmende Fall, groesser als programs.json (Phase 14).
constexpr size_t kMaxJsonPayloadSize =
    FileSystem::kScheduleJsonCapacity > FileSystem::kProgramsJsonCapacity ? FileSystem::kScheduleJsonCapacity
                                                                             : FileSystem::kProgramsJsonCapacity;

// Grenzen fuer time/set (Minuten). Obere Grenze ist ein grosszuegiger Sanity-Check,
// die eigentliche Deckelung der effektiven Laufzeit erfolgt ueber maxTime (ValveTimer).
constexpr long kMinValveTimeMinutes = 1;
constexpr long kMaxValveTimeMinutes = 999;

// Sekuendlicher Countdown-Tick ist sicherheitskritisch (Ventil-Abschaltung bei
// Zeitablauf) und muss unabhaengig von WLAN/MQTT laufen, siehe docs/requirements.md
// ("laeuft lokal/autonom weiter").
constexpr unsigned long kTickIntervalMs = 1000;

// Analog zum Reconnect-Intervall aus WiFiController: kurz genug fuer zuegige
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

// Ringpuffer der letzten Log-Zeilen (immer aktiv, nicht nur waehrend einer Trennung) -
// diagnostics/livelog ist bewusst nicht retained (siehe kLiveLogTopic), MQTT-Retain
// haette hier ohnehin nur die jeweils letzte Zeile behalten, kein Verlauf. Web-Clients
// bekaeman ohne diesen Puffer beim Oeffnen der Log-Seite nur kuenftige Zeilen zu sehen -
// per replayLogBuffer() liefert das Geraet stattdessen auf Anfrage (diagnostics/
// livelog/replay) den kompletten aktuellen Puffer nach. Aeltester Eintrag faellt raus,
// wenn der Puffer voll ist (80 * Logger::kMaxLineLength Byte ~= 44 KB, RAM-Headroom
// reichlich vorhanden). Zeilengroesse bewusst identisch zu Logger::kMaxLineLength statt
// eines eigenen Literals - sonst genau die Art von Bug, die einmal V{n}/auto/state
// abgeschnitten hat (siehe kTopicBufferSize-Kommentar oben).
constexpr uint8_t kLogBufferCapacity = 80;
char logBuffer[kLogBufferCapacity][Logger::kMaxLineLength];
uint8_t logBufferCount = 0;
uint8_t logBufferStart = 0;
// Wie viele der zuletzt gepufferten Zeilen noch nicht live publiziert wurden - siehe
// flushPendingLogLines() fuer den Grund, warum das nicht direkt in appendToLogBuffer()/
// onLoggerLine() passiert.
uint8_t pendingLogCount = 0;
// Anfrage ueber diagnostics/livelog/replay kommt aus handleMqttMessage() (MQTT-Callback-
// Kontext) - aus demselben Grund wie pendingLogCount nicht dort direkt ausfuehren, sondern
// nur vormerken und in flushPendingLogLines() (sicherer Kontext) abarbeiten.
bool deferredReplayRequested = false;

void appendToLogBuffer(const char *line) {
  const uint8_t writeIndex = (logBufferStart + logBufferCount) % kLogBufferCapacity;
  strncpy(logBuffer[writeIndex], line, sizeof(logBuffer[writeIndex]) - 1);
  logBuffer[writeIndex][sizeof(logBuffer[writeIndex]) - 1] = '\0';
  if (logBufferCount < kLogBufferCapacity) {
    logBufferCount++;
  } else {
    logBufferStart = (logBufferStart + 1) % kLogBufferCapacity;  // aeltesten Eintrag verwerfen
  }
}

// Live-Log-Weiterleitung (Logger::setLineCallback(), Nachtrag 2026-08-18): puffert nur, OHNE
// direkt zu publizieren - siehe flushPendingLogLines() fuer den Grund. Filtert vorher die
// zwei sekuendlich (nicht retained) publizierenden Live-Werte main/remainingTotal und
// V{n}/time/remaining heraus - die wuerden das Live-Log waehrend einer laufenden Automatik-
// Sequenz reinem Rauschen unterordnen, ohne Zusatzinfo gegenueber main/state/main/activeValve.
void onLoggerLine(const char *line) {
  if (strstr(line, "remainingTotal =") != nullptr || strstr(line, "time/remaining =") != nullptr) {
    return;
  }
  appendToLogBuffer(line);
  if (pendingLogCount < kLogBufferCapacity) {
    pendingLogCount++;
  }
}

// Publiziert alle seit dem letzten Aufruf gepufferten Log-Zeilen. WICHTIG: dies NIEMALS
// direkt aus onLoggerLine()/Logger::log() heraus aufrufen, sondern nur von einer Stelle, die
// garantiert ausserhalb eines eingehenden MQTT-Callbacks laeuft (hier: am Ende von
// MQTT::loop(), nach mqttClient.loop()). Grund: PubSubClient nutzt einen gemeinsamen
// internen Puffer fuer ein- und ausgehende Pakete; das an handleMqttMessage() uebergebene
// `topic` zeigt vermutlich direkt in diesen Puffer. Ein publish() WAEHREND der Verarbeitung
// einer eingehenden Nachricht ueberschreibt diesen Puffer und macht `topic` damit fuer die
// anschliessenden strcmp()-Vergleiche zu Datenmuell - auf Hardware reproduziert: jede SUB-
// Zeile erzeugte durch den (damals noch synchronen) Live-Log-Publish direkt danach eine
// falsche "Unbekanntes Topic"-ERROR-Zeile fuer dieselbe, eigentlich gueltige Nachricht.
void flushPendingLogLines() {
  if (!mqttClient.connected()) {
    return;
  }
  if (deferredReplayRequested) {
    // Kompletter Puffer inkl. der noch "pending" Zeilen - deckt beides in einem Rutsch ab.
    for (uint8_t i = 0; i < logBufferCount; i++) {
      mqttClient.publish(kLiveLogTopic, logBuffer[(logBufferStart + i) % kLogBufferCapacity], false);
    }
    deferredReplayRequested = false;
    pendingLogCount = 0;
    return;
  }
  if (pendingLogCount == 0) {
    return;
  }
  const uint8_t startOffset = logBufferCount - pendingLogCount;
  for (uint8_t i = 0; i < pendingLogCount; i++) {
    const uint8_t idx = (logBufferStart + startOffset + i) % kLogBufferCapacity;
    mqttClient.publish(kLiveLogTopic, logBuffer[idx], false);
  }
  pendingLogCount = 0;
}

// Sendet den kompletten aktuellen Ringpuffer erneut raus - einerseits automatisch nach jedem
// erfolgreichen (Re-)Connect (holt eine Verbindungsluecke nach, z.B. die Boot-Sequenz vor dem
// allerersten Connect), andererseits auf Anfrage eines Web-Clients (diagnostics/livelog/
// replay), der die Log-Seite gerade erst geoeffnet hat und sonst nur kuenftige Zeilen saehe.
void replayLogBuffer() {
  for (uint8_t i = 0; i < logBufferCount; i++) {
    mqttClient.publish(kLiveLogTopic, logBuffer[(logBufferStart + i) % kLogBufferCapacity], false);
  }
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
  snprintf(payload, sizeof(payload), "%u", FileSystem::getMaxTime());
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

// Laenge (siehe FileSystem::kAliasMaxLength) und Steuerzeichen pruefen. UTF-8-
// Mehrbyte-Folgen (Umlaute etc., Bytes >= 0x80) sind ausdruecklich erlaubt.
bool isValidAliasPayload(const char *payloadStr, size_t length) {
  if (length > FileSystem::kAliasMaxLength) {
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
  char payload[FileSystem::kJsonCapacity];
  const size_t written = FileSystem::toJson(payload, sizeof(payload));
  if (written == 0) {
    Logger::log(Logger::Type::ERROR, Logger::Source::MQTT, "main/config/state: Serialisierung fehlgeschlagen.");
    return;
  }
  publishAndLog(kConfigStateTopic, payload, true);
}

// Publiziert das aktuell gewaehlte Programm (Singular, kompakt), retained -
// bei jeder Aenderung und nach jedem (Re-)Connect (siehe docs/spec/14-programme.md).
void publishProgramState() {
  const uint8_t active = FileSystem::getActiveProgram();
  StaticJsonDocument<96> doc;
  doc["index"] = active;
  if (active == 0 || active > FileSystem::getProgramCount()) {
    doc["name"] = nullptr;
  } else {
    doc["name"] = FileSystem::getProgramName(active);
  }
  char payload[96];
  serializeJson(doc, payload, sizeof(payload));
  publishAndLog(kProgramStateTopic, payload, true);
}

// Publiziert die komplette Programme-Liste + activeProgram als JSON, retained -
// bei jeder Aenderung (egal welches Topic) und nach jedem (Re-)Connect.
void publishProgramsState() {
  char payload[FileSystem::kProgramsJsonCapacity];
  const size_t written = FileSystem::programsToJson(payload, sizeof(payload));
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

void publishVersion() {
  // kBuildNumber (include/BuildNumber.h) wird bei jedem Build automatisch erzeugt
  // (tools/increment_build_number.py) - kFirmwareVersion bleibt weiterhin manuell gepflegt.
  char payload[48];
  snprintf(payload, sizeof(payload), "%s Build %s", kFirmwareVersion, kBuildNumber);
  publishAndLog(kVersionTopic, payload, true);
}

// RAM: aktueller Heap (ESP.getFreeHeap()/getHeapSize()) - dynamisch, daher periodisch
// aktualisiert statt nur einmalig. Flash: Sketch-Groesse vs. freier Platz im App-Slot
// (ESP.getSketchSize()/getFreeSketchSpace()) - entspricht der Groessenordnung, die auch
// "pio run" beim Bauen als "Flash: XX%" ausweist.
void publishHardwareStatus() {
  const uint32_t heapTotal = ESP.getHeapSize();
  const uint32_t heapFree = ESP.getFreeHeap();
  const uint32_t heapUsed = heapTotal - heapFree;
  char ramPayload[40];
  snprintf(ramPayload, sizeof(ramPayload), "%lu%% (%lu/%lu KB)",
           heapTotal > 0 ? (100UL * heapUsed) / heapTotal : 0, heapUsed / 1024, heapTotal / 1024);
  publishAndLog(kRamTopic, ramPayload, true);

  const uint32_t flashUsed = ESP.getSketchSize();
  const uint32_t flashTotal = flashUsed + ESP.getFreeSketchSpace();
  char flashPayload[40];
  snprintf(flashPayload, sizeof(flashPayload), "%lu%% (%lu/%lu KB)",
           flashTotal > 0 ? (100UL * flashUsed) / flashTotal : 0, flashUsed / 1024, flashTotal / 1024);
  publishAndLog(kFlashTopic, flashPayload, true);
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

  static unsigned long lastHardwareStatusMs = 0 - kHardwareStatusIntervalMs;  // sofort beim ersten Tick
  const unsigned long now = millis();
  if (now - lastHardwareStatusMs >= kHardwareStatusIntervalMs) {
    lastHardwareStatusMs = now;
    publishHardwareStatus();
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
  if (!AutomaticController::isRunning()) {
    return 0;
  }
  uint32_t total = ValveTimer::getRemainingSeconds(AutomaticController::getActiveValve());
  const uint8_t pendingCount = AutomaticController::getPendingCount();
  for (uint8_t i = 0; i < pendingCount; i++) {
    const uint8_t v = AutomaticController::getPendingValve(i);
    const uint16_t timeMinutes = FileSystem::getValveTime(v);
    const uint16_t maxTimeMinutes = FileSystem::getMaxTime();
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
  const uint8_t next = AutomaticController::advance();
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

// Gemeinsamer Nachtritt fuer jede DIREKTE time/auto-Aenderung (V{n}/time|auto/set,
// main/config/set - NICHT das Anwenden eines Programms selbst, das ruft applyTimeValue()/
// applyAutoValue() direkt auf und bleibt unberuehrt): startSequence() wendet das aktive
// Programm vor jedem Start ohnehin erneut an, um Drift durch zwischenzeitliche manuelle
// Aenderungen zu verhindern (siehe dort) - ein manueller Zwischenstand wuerde beim naechsten
// Start also unsichtbar verworfen. Um das nicht als stillen Widerspruch zwischen Anzeige und
// tatsaechlicher Wirkung stehen zu lassen, beendet eine direkte Aenderung stattdessen sofort
// die Programmzuordnung (= "MANUELL" im UI, inkl. des bereits bestehenden Auto-Resets aus
// applyProgram(0)) - Ventile lassen sich danach nur noch einzeln manuell schalten.
void publishConfigStateAndClearProgram() {
  const uint8_t previous = FileSystem::getActiveProgram();
  if (previous != 0) {
    Logger::logf(Logger::Type::INFO, Logger::Source::MQTT,
                 "MANUELL: Programm '%s' durch direkte Aenderung abgewaehlt.", FileSystem::getProgramName(previous));
    applyProgram(0);  // publiziert config/program/programs-State bereits
  } else {
    publishConfigState();
  }
}

void startSequence() {
  if (AutomaticController::isRunning()) {
    Logger::log(Logger::Type::INFO, Logger::Source::MQTT, "main/cmd ON ignoriert (Automatik laeuft bereits).");
    return;
  }

  const uint8_t activeProgram = FileSystem::getActiveProgram();
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

  if (!AutomaticController::start(autoValves, count)) {
    Logger::log(Logger::Type::ERROR, Logger::Source::MQTT, "main/cmd ON ignoriert: kein Ventil mit auto=ON.");
    return;
  }

  publishMainState(true);
  const uint8_t first = AutomaticController::getActiveValve();
  applyValveCommand(first, true);
  publishActiveValve(first);
  publishRemainingTotalNow();
}

void stopSequence() {
  const bool wasRunning = AutomaticController::isRunning();
  const uint8_t active = AutomaticController::getActiveValve();
  if (active != 0) {
    applyValveCommand(active, false);
  }
  AutomaticController::stop();
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

// Kernlogik ohne Payload-Parsing, wiederverwendet von MQTT::requestValveCmd()
// (Touch-UI-Ventilmatrix, Touch-UI-Neugestaltung, kein MQTT-Umweg).
void applyValveCmd(uint8_t index, bool targetOn) {
  // Waehrend die Automatik laeuft: manuelles ON ignorieren (siehe
  // docs/spec/07-automatik-sequenz.md); manuelles OFF des aktiven Ventils
  // wird angenommen und stoesst den naechsten Schritt der Sequenz an.
  if (targetOn && AutomaticController::isRunning()) {
    Logger::logf(Logger::Type::INFO, Logger::Source::MQTT, "V%u/cmd ON ignoriert (Automatik laeuft).", index);
    return;
  }

  const bool wasActiveSequenceValve = AutomaticController::isRunning() && AutomaticController::getActiveValve() == index;
  applyValveCommand(index, targetOn);
  if (!targetOn) {
    if (wasActiveSequenceValve) {
      advanceSequence();  // Restlaufzeit bleibt 0, bis die ganze Sequenz endet
    } else {
      armIdleValve(index);  // normaler manueller Stopp: sofort wieder auf `time` armiert
    }
  }
}

void handleValveCmd(uint8_t index, const char *payloadStr) {
  bool targetOn = false;
  if (!parseOnOffPayload(payloadStr, &targetOn)) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Payload '%s' fuer V%u/cmd", payloadStr,
                 index);
    return;
  }
  applyValveCmd(index, targetOn);
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
  publishConfigStateAndClearProgram();
}

// Kernlogik ohne Payload-Parsing, wiederverwendet von main/config/set
// (dort kommt der Wert direkt als JSON-Zahl statt als String).
void applyTimeValue(uint8_t index, long value) {
  if (value < kMinValveTimeMinutes || value > kMaxValveTimeMinutes) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Wert '%ld' fuer V%u/time", value, index);
    return;
  }

  const uint16_t minutes = static_cast<uint16_t>(value);
  FileSystem::setValveTime(index, minutes);
  publishTimeState(index, minutes);

  // Ventil laeuft gerade nicht und es laeuft auch keine Automatik-Sequenz:
  // angezeigte Restlaufzeit sofort an die neue Konfiguration anpassen (armiert),
  // statt auf den naechsten Start zu warten. Waehrend einer laufenden Sequenz
  // NICHT re-armieren - sonst sieht ein bereits durchgelaufenes Ventil so aus,
  // als waere es noch an der Reihe (siehe armIdleValve()/advanceSequence()).
  if (!ValveController::getValve(index) && !AutomaticController::isRunning()) {
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
  publishConfigStateAndClearProgram();
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
  FileSystem::setMaxTime(static_cast<uint16_t>(value));
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
  StaticJsonDocument<FileSystem::kJsonCapacity> doc;
  const DeserializationError err = deserializeJson(doc, payloadStr);
  if (err) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/config/set: JSON-Fehler: %s", err.c_str());
    return;
  }

  if (!doc["maxTime"].isNull()) {
    applyMaxTimeValue(doc["maxTime"].as<long>());
  }

  // Nur time/auto gehoeren zu einem Programm (siehe applyProgram()) - maxTime/alias loesen
  // deshalb bewusst KEIN "MANUELL" aus, siehe publishConfigStateAndClearProgram().
  bool touchedTimeOrAuto = false;

  uint8_t index = 0;
  JsonObjectConst timeObj = doc["time"];
  for (JsonPairConst kv : timeObj) {
    if (parseValveKey(kv.key().c_str(), 1, 5, &index)) {
      applyTimeValue(index, kv.value().as<long>());
      touchedTimeOrAuto = true;
    } else {
      Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/config/set: unbekanntes Ventil '%s' in time",
                   kv.key().c_str());
    }
  }

  JsonObjectConst autoObj = doc["auto"];
  for (JsonPairConst kv : autoObj) {
    if (parseValveKey(kv.key().c_str(), 1, 5, &index)) {
      applyAutoValue(index, kv.value().as<bool>());
      touchedTimeOrAuto = true;
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

  if (touchedTimeOrAuto) {
    publishConfigStateAndClearProgram();
  } else {
    publishConfigState();
  }
}

// Kernfunktion (Phase 14): wendet Programm `programIndex` (1-basiert) an, indem fuer jedes im
// Programm enthaltene Feld dieselben applyTimeValue()/applyAutoValue()-Kernfunktionen wie
// main/config/set aufgerufen werden (siehe docs/spec/14-programme.md, Kernentscheidung 3).
// `programIndex == 0` loescht die Auswahl UND setzt alle auto-Flags zurueck (Nachtrag
// 2026-08-17) - sonst bliebe der auto-Zustand des zuletzt aktiven Programms an den Ventilen
// haengen, obwohl die Anzeige "Kein Programm" zeigt. Wiederverwendet von main/program/cmd
// und dem Key "activeProgram" in main/programs/set.
void applyProgram(uint8_t programIndex) {
  if (programIndex != 0) {
    if (programIndex > FileSystem::getProgramCount()) {
      Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Programm-Index '%u'", programIndex);
      return;
    }
    for (uint8_t v = 1; v <= 5; v++) {
      if (FileSystem::programHasTime(programIndex, v)) {
        applyTimeValue(v, FileSystem::getProgramTime(programIndex, v));
      }
      if (FileSystem::programHasAuto(programIndex, v)) {
        applyAutoValue(v, FileSystem::getProgramAuto(programIndex, v));
      }
    }
    Logger::logf(Logger::Type::INFO, Logger::Source::MQTT, "Programm '%s' angewendet.",
                 FileSystem::getProgramName(programIndex));
  } else {
    for (uint8_t v = 1; v <= 5; v++) {
      applyAutoValue(v, false);
    }
  }
  FileSystem::setActiveProgram(programIndex);
  publishProgramState();
  publishProgramsState();
  publishConfigState();  // time/auto koennen sich durchs Anwenden geaendert haben
}

void handleProgramCmd(const char *payloadStr) {
  char *endPtr = nullptr;
  const long value = strtol(payloadStr, &endPtr, 10);
  if (endPtr == payloadStr || *endPtr != '\0' || value < 0 || value > FileSystem::kMaxPrograms) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Wert '%s' fuer main/program/cmd",
                 payloadStr);
    return;
  }
  applyProgram(static_cast<uint8_t>(value));
}

// main/programs/set: "programs" ersetzt das komplette Array (kein Feld-Merge einzelner
// Programme, siehe docs/spec/14-programme.md, Kernentscheidung 5), "activeProgram" wendet
// die Auswahl an (identisch zu main/program/cmd, ueber applyProgram()). Beide optional.
void handleProgramsSet(const char *payloadStr) {
  StaticJsonDocument<FileSystem::kProgramsJsonCapacity> doc;
  const DeserializationError err = deserializeJson(doc, payloadStr);
  if (err) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/programs/set: JSON-Fehler: %s", err.c_str());
    return;
  }

  JsonArrayConst programsArr = doc["programs"];
  if (!programsArr.isNull()) {
    FileSystem::ProgramInput entries[FileSystem::kMaxPrograms];
    uint8_t count = 0;
    for (JsonObjectConst obj : programsArr) {
      if (count >= FileSystem::kMaxPrograms) {
        Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT,
                     "main/programs/set: mehr als %u Programme, Rest wird ignoriert.", FileSystem::kMaxPrograms);
        break;
      }
      FileSystem::ProgramInput &entry = entries[count];
      entry.name = obj["name"] | "";
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
    FileSystem::setPrograms(entries, count);
  }

  if (!doc["activeProgram"].isNull()) {
    const long value = doc["activeProgram"].as<long>();
    if (value < 0 || value > FileSystem::kMaxPrograms) {
      Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/programs/set: ungueltiger activeProgram-Wert '%ld'",
                   value);
    } else {
      applyProgram(static_cast<uint8_t>(value));  // publiziert programs/program/config-State bereits
      return;
    }
  }

  publishProgramsState();
}

// --- Zeitplan (Phase 15) ------------------------------------------------------------------

constexpr const char *kWeekdayLabels[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

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

bool parseScheduleTypeValue(const char *value, FileSystem::ScheduleType *outType) {
  if (value == nullptr) {
    return false;
  }
  if (strcmp(value, "daily") == 0) {
    *outType = FileSystem::ScheduleType::DAILY;
    return true;
  }
  if (strcmp(value, "weekly") == 0) {
    *outType = FileSystem::ScheduleType::WEEKLY;
    return true;
  }
  if (strcmp(value, "once") == 0) {
    *outType = FileSystem::ScheduleType::ONCE;
    return true;
  }
  return false;
}

// Parst "HH:MM" (exakt 5 Zeichen, Doppelpunkt an Position 2) in Stunde/Minute.
bool parseScheduleTimeValue(const char *value, uint8_t *outHour, uint8_t *outMinute) {
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

// Parst "YYYY-MM-DD" (ISO 8601, exakt 10 Zeichen) in Jahr/Monat/Tag.
bool parseScheduleDateValue(const char *value, uint16_t *outYear, uint8_t *outMonth, uint8_t *outDay) {
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

void publishScheduleState() {
  char payload[FileSystem::kScheduleJsonCapacity];
  const size_t written = FileSystem::scheduleToJson(payload, sizeof(payload));
  if (written == 0) {
    Logger::log(Logger::Type::ERROR, Logger::Source::MQTT, "main/schedule/state: Serialisierung fehlgeschlagen.");
    return;
  }
  publishAndLog(kScheduleStateTopic, payload, true);
}

// main/schedule/set: "schedule" ersetzt das komplette Array (wie main/programs/set,
// kein Feld-Merge einzelner Eintraege), "enabled" setzt den globalen Schalter. Beide
// optional. Ungueltige Einzeleintraege werden übersprungen + geloggt, nicht die ganze
// Anfrage abgelehnt (wie ueberall sonst im Projekt).
void handleScheduleSet(const char *payloadStr) {
  StaticJsonDocument<FileSystem::kScheduleJsonCapacity> doc;
  const DeserializationError err = deserializeJson(doc, payloadStr);
  if (err) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "main/schedule/set: JSON-Fehler: %s", err.c_str());
    return;
  }

  JsonArrayConst scheduleArr = doc["schedule"];
  if (!scheduleArr.isNull()) {
    FileSystem::ScheduleInput entries[FileSystem::kMaxScheduleEntries];
    uint8_t count = 0;
    for (JsonObjectConst obj : scheduleArr) {
      if (count >= FileSystem::kMaxScheduleEntries) {
        Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT,
                     "main/schedule/set: mehr als %u Eintraege, Rest wird ignoriert.",
                     FileSystem::kMaxScheduleEntries);
        break;
      }
      FileSystem::ScheduleInput &entry = entries[count];
      entry.program = obj["program"] | "";
      if (entry.program[0] == '\0') {
        Logger::log(Logger::Type::ERROR, Logger::Source::MQTT,
                     "main/schedule/set: Eintrag ohne 'program', uebersprungen.");
        continue;
      }
      entry.enabled = obj["enabled"] | true;

      FileSystem::ScheduleType type = FileSystem::ScheduleType::DAILY;
      if (!parseScheduleTypeValue(obj["type"] | "", &type)) {
        Logger::log(Logger::Type::ERROR, Logger::Source::MQTT,
                     "main/schedule/set: ungueltiger oder fehlender 'type', Eintrag uebersprungen.");
        continue;
      }
      entry.type = type;

      uint8_t hour = 0;
      uint8_t minute = 0;
      if (!parseScheduleTimeValue(obj["time"] | "", &hour, &minute)) {
        Logger::log(Logger::Type::ERROR, Logger::Source::MQTT,
                     "main/schedule/set: ungueltige oder fehlende 'time', Eintrag uebersprungen.");
        continue;
      }
      entry.hour = hour;
      entry.minute = minute;

      entry.weekdaysMask = 0;
      if (type == FileSystem::ScheduleType::WEEKLY) {
        JsonArrayConst weekdaysArr = obj["weekdays"];
        for (JsonVariantConst wd : weekdaysArr) {
          const uint8_t bit = parseWeekdayLabel(wd.as<const char *>());
          if (bit != 0xFF) {
            entry.weekdaysMask |= (1 << bit);
          } else {
            Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT,
                         "main/schedule/set: unbekannter Wochentag '%s' ignoriert.", wd.as<const char *>());
          }
        }
        if (entry.weekdaysMask == 0) {
          Logger::log(Logger::Type::ERROR, Logger::Source::MQTT,
                       "main/schedule/set: 'weekly'-Eintrag ohne gueltige 'weekdays', uebersprungen.");
          continue;
        }
      }

      entry.year = 0;
      entry.month = 0;
      entry.day = 0;
      if (type == FileSystem::ScheduleType::ONCE) {
        uint16_t year = 0;
        uint8_t month = 0;
        uint8_t day = 0;
        if (!parseScheduleDateValue(obj["date"] | "", &year, &month, &day)) {
          Logger::log(Logger::Type::ERROR, Logger::Source::MQTT,
                       "main/schedule/set: ungueltiges oder fehlendes 'date' fuer 'once'-Eintrag, uebersprungen.");
          continue;
        }
        entry.year = year;
        entry.month = month;
        entry.day = day;
      }

      count++;
    }
    FileSystem::setSchedule(entries, count);
  }

  if (!doc["enabled"].isNull()) {
    FileSystem::setScheduleGlobalEnabled(doc["enabled"].as<bool>());
  }

  publishScheduleState();
}

void handleScheduleCmd(const char *payloadStr) {
  bool targetOn = false;
  if (!parseOnOffPayload(payloadStr, &targetOn)) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Payload '%s' fuer main/schedule/cmd",
                 payloadStr);
    return;
  }
  FileSystem::setScheduleGlobalEnabled(targetOn);
  publishScheduleState();
}

// Entfernt alle "once"-Eintraege, deren Datum in der Vergangenheit liegt (koennen nie
// wieder auslösen, siehe docs/spec/15-wochenplan.md) - auf Anfrage, kein Automatismus.
// Statische Puffer statt Stack-Arrays, um den ohnehin schon knappen loopTask-Stack nicht
// weiter zu belasten (siehe Phase-14-Stack-Overflow-Erfahrung).
void handleScheduleCleanup() {
  if (!Logger::isRealTimeEnabled()) {
    Logger::log(Logger::Type::ERROR, Logger::Source::MQTT,
                 "main/schedule/cleanup: Echtzeit nicht verfuegbar, abgebrochen.");
    return;
  }
  const time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  const uint16_t currentYear = static_cast<uint16_t>(timeinfo.tm_year + 1900);
  const uint8_t currentMonth = static_cast<uint8_t>(timeinfo.tm_mon + 1);
  const uint8_t currentDay = static_cast<uint8_t>(timeinfo.tm_mday);

  const uint8_t count = FileSystem::getScheduleCount();
  static char programBuf[FileSystem::kMaxScheduleEntries][FileSystem::kAliasMaxLength + 1];
  FileSystem::ScheduleInput entries[FileSystem::kMaxScheduleEntries];
  uint8_t keepCount = 0;
  uint8_t removedCount = 0;
  for (uint8_t i = 0; i < count; i++) {
    bool expired = false;
    if (FileSystem::getScheduleType(i) == FileSystem::ScheduleType::ONCE) {
      const uint16_t y = FileSystem::getScheduleYear(i);
      const uint8_t m = FileSystem::getScheduleMonth(i);
      const uint8_t d = FileSystem::getScheduleDay(i);
      if (y < currentYear || (y == currentYear && m < currentMonth) ||
          (y == currentYear && m == currentMonth && d < currentDay)) {
        expired = true;
      }
    }
    if (expired) {
      removedCount++;
      continue;
    }

    strncpy(programBuf[keepCount], FileSystem::getScheduleProgram(i), FileSystem::kAliasMaxLength);
    programBuf[keepCount][FileSystem::kAliasMaxLength] = '\0';

    FileSystem::ScheduleInput &entry = entries[keepCount];
    entry.program = programBuf[keepCount];
    entry.enabled = FileSystem::getScheduleEnabled(i);
    entry.type = FileSystem::getScheduleType(i);
    entry.hour = FileSystem::getScheduleHour(i);
    entry.minute = FileSystem::getScheduleMinute(i);
    entry.weekdaysMask = FileSystem::getScheduleWeekdaysMask(i);
    entry.year = FileSystem::getScheduleYear(i);
    entry.month = FileSystem::getScheduleMonth(i);
    entry.day = FileSystem::getScheduleDay(i);
    keepCount++;
  }

  if (removedCount > 0) {
    FileSystem::setSchedule(entries, keepCount);
    publishScheduleState();
  }
  Logger::logf(Logger::Type::INFO, Logger::Source::MQTT, "main/schedule/cleanup: %u abgelaufene Eintraege entfernt.",
               removedCount);
}

// Sicherheitskritisch im weiteren Sinne (lokale Zeitsteuerung soll wie der Ventil-Tick
// unabhaengig von WLAN/MQTT laufen, siehe Aufrufstelle in MQTT::loop()). Prueft
// einmal pro Minute (siehe docs/spec/15-wochenplan.md, Kernentscheidung 4) die komplette
// schedule-Liste gegen die aktuelle Zeit - kein Timer pro Eintrag.
unsigned long lastCheckedScheduleMinute = 0xFFFFFFFFUL;  // Sentinel: noch nie geprueft

void checkSchedule() {
  if (!Logger::isRealTimeEnabled() || !FileSystem::getScheduleGlobalEnabled()) {
    return;
  }

  const time_t now = time(nullptr);
  const unsigned long currentMinuteId = static_cast<unsigned long>(now / 60);
  if (currentMinuteId == lastCheckedScheduleMinute) {
    return;  // diese Minute schon geprueft
  }
  lastCheckedScheduleMinute = currentMinuteId;

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  const uint8_t currentHour = static_cast<uint8_t>(timeinfo.tm_hour);
  const uint8_t currentMinute = static_cast<uint8_t>(timeinfo.tm_min);
  const uint8_t currentWeekdayBit = static_cast<uint8_t>((timeinfo.tm_wday + 6) % 7);  // 0=Montag..6=Sonntag
  const uint16_t currentYear = static_cast<uint16_t>(timeinfo.tm_year + 1900);
  const uint8_t currentMonth = static_cast<uint8_t>(timeinfo.tm_mon + 1);
  const uint8_t currentDay = static_cast<uint8_t>(timeinfo.tm_mday);

  const uint8_t count = FileSystem::getScheduleCount();
  for (uint8_t i = 0; i < count; i++) {
    if (!FileSystem::getScheduleEnabled(i)) {
      continue;
    }
    if (FileSystem::getScheduleHour(i) != currentHour || FileSystem::getScheduleMinute(i) != currentMinute) {
      continue;
    }
    const FileSystem::ScheduleType type = FileSystem::getScheduleType(i);
    if (type == FileSystem::ScheduleType::WEEKLY) {
      if (!(FileSystem::getScheduleWeekdaysMask(i) & (1 << currentWeekdayBit))) {
        continue;
      }
    } else if (type == FileSystem::ScheduleType::ONCE) {
      if (FileSystem::getScheduleYear(i) != currentYear || FileSystem::getScheduleMonth(i) != currentMonth ||
          FileSystem::getScheduleDay(i) != currentDay) {
        continue;
      }
    }
    // type == DAILY: Uhrzeit hat schon gepasst, kein weiterer Check noetig.

    const char *programName = FileSystem::getScheduleProgram(i);
    const uint8_t programIndex = FileSystem::getProgramIndexForName(programName);
    if (programIndex == 0) {
      Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Zeitplan: Programm '%s' (Eintrag %u) nicht gefunden.",
                   programName, i);
      continue;
    }
    Logger::logf(Logger::Type::INFO, Logger::Source::MQTT, "Zeitplan: Eintrag %u ausgeloest, Programm '%s'.", i,
                 programName);
    applyProgram(programIndex);
    startSequence();  // Guard (AutomaticController::isRunning()) loest "gleichzeitige Trigger" automatisch auf (siehe Spec)
  }
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
  if (strcmp(topic, kScheduleSetTopic) == 0) {
    handleScheduleSet(payloadStr);
    return;
  }
  if (strcmp(topic, kScheduleCmdTopic) == 0) {
    handleScheduleCmd(payloadStr);
    return;
  }
  if (strcmp(topic, kScheduleCleanupTopic) == 0) {
    handleScheduleCleanup();
    return;
  }
  if (strcmp(topic, kLiveLogReplayTopic) == 0) {
    deferredReplayRequested = true;  // nicht direkt hier ausfuehren, siehe flushPendingLogLines()
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
// Aufrufstelle in MQTT::loop()). publish()-Aufrufe sind bei fehlender
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
      const bool wasActiveSequenceValve = AutomaticController::isRunning() && AutomaticController::getActiveValve() == i;
      applyValveCommand(i, false);
      if (wasActiveSequenceValve) {
        advanceSequence();  // Restlaufzeit bleibt 0, bis die ganze Sequenz endet
      } else {
        armIdleValve(i);  // normaler Zeitablauf ausserhalb einer Sequenz: sofort re-armiert
      }
    }
  }
  if (AutomaticController::isRunning()) {
    publishRemainingTotalNow();
  }
}

bool connectToBroker() {
  const bool ok = mqttClient.connect(MQTT_CLIENT_ID, kAvailabilityTopic, kAvailabilityQos, true, kOfflinePayload);
  if (ok) {
    replayLogBuffer();  // Verbindungsluecke nachholen, vor der ersten "echten" Zeile danach
    Logger::log(Logger::Type::INFO, Logger::Source::MQTT, "Verbunden.");
    publishAndLog(kAvailabilityTopic, kOnlinePayload, true);
    mqttClient.subscribe(kMainCmdTopic);
    mqttClient.subscribe(kV0AliasSetTopic);
    mqttClient.subscribe(kConfigSetTopic);
    mqttClient.subscribe(kProgramCmdTopic);
    mqttClient.subscribe(kProgramsSetTopic);
    mqttClient.subscribe(kScheduleSetTopic);
    mqttClient.subscribe(kScheduleCmdTopic);
    mqttClient.subscribe(kScheduleCleanupTopic);
    mqttClient.subscribe(kLiveLogReplayTopic);
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
      publishTimeState(i, FileSystem::getValveTime(i));
      publishRemaining(i, ValveTimer::getRemainingSeconds(i));
      publishAutoState(i, ValveController::getAuto(i));
      publishAlias(i, ValveController::getAlias(i));
    }
    publishMaxTime();
    publishMainState(AutomaticController::isRunning());
    publishActiveValve(AutomaticController::getActiveValve());
    publishRemainingTotalNow();
    publishI2cStatus(Diagnostics::isI2cOk());
    if (Diagnostics::getLastError()[0] != '\0') {
      publishLastError(Diagnostics::getLastError());
    }
    publishVersion();
    publishHardwareStatus();
    publishConfigState();
    publishProgramState();
    publishProgramsState();
    publishScheduleState();
  }
  return ok;
}

}  // namespace

void MQTT::begin() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(handleMqttMessage);
  // Default (256 Byte) reicht nicht fuer die JSON-Topics (main/config/*, main/programs/*).
  // main/programs/set|state (Phase 14, bis zu kMaxPrograms Eintraege) ist der groesste Fall.
  mqttClient.setBufferSize(static_cast<uint16_t>(kMaxJsonPayloadSize));
  Logger::setLineCallback(&onLoggerLine);
}

void MQTT::loop() {
  const unsigned long now = millis();
  if (now - lastTickMs >= kTickIntervalMs) {
    lastTickMs = now;
    tickValveTimers();
    checkDiagnostics();
    checkSchedule();  // laeuft wie die anderen beiden unabhaengig von WLAN/MQTT (siehe checkSchedule())
  }

  if (!WiFiController::isConnected()) {
    // Ohne WLAN ist ein MQTT-Reconnect-Versuch sinnlos; Status als getrennt fuehren.
    wasConnected = false;
    return;
  }

  mqttClient.loop();
  // Erst NACH mqttClient.loop() - siehe flushPendingLogLines() fuer den Grund
  // (PubSubClient-Pufferkonflikt, wenn stattdessen direkt aus dem Empfangs-Callback publiziert wird).
  flushPendingLogLines();
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

bool MQTT::isConnected() {
  return mqttClient.connected();
}

void MQTT::requestMainCmd(bool on) {
  if (on) {
    startSequence();
  } else {
    stopSequence();
  }
}

void MQTT::requestProgramSelect(uint8_t programIndex) {
  applyProgram(programIndex);
}

void MQTT::requestValveCmd(uint8_t index, bool on) {
  applyValveCmd(index, on);
}
