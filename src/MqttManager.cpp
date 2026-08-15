#include "MqttManager.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cstdlib>
#include <cstring>

#include "ConfigStore.h"
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

constexpr char kValvePrefix[] = "gartenwasser/V";
constexpr char kCmdSuffix[] = "/cmd";
constexpr char kTimeSetSuffix[] = "/time/set";
constexpr char kAutoSetSuffix[] = "/auto/set";

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
  char topic[24];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/state", index);
  publishAndLog(topic, on ? "ON" : "OFF", true);
}

void publishTimeState(uint8_t index, uint16_t minutes) {
  char topic[32];
  char payload[8];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/time/state", index);
  snprintf(payload, sizeof(payload), "%u", minutes);
  publishAndLog(topic, payload, true);
}

void publishRemaining(uint8_t index, uint16_t seconds) {
  char topic[32];
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
  char topic[32];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/auto/state", index);
  publishAndLog(topic, on ? "ON" : "OFF", true);
}

void publishMainState(bool running) {
  publishAndLog(kMainStateTopic, running ? "ON" : "OFF", true);
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

void startSequence() {
  if (Sequencer::isRunning()) {
    Logger::log(Logger::Type::INFO, Logger::Source::MQTT, "main/cmd ON ignoriert (Automatik laeuft bereits).");
    return;
  }

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

void handleAutoSet(uint8_t index, const char *payloadStr) {
  bool targetOn = false;
  if (!parseOnOffPayload(payloadStr, &targetOn)) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Payload '%s' fuer V%u/auto/set", payloadStr,
                 index);
    return;
  }
  ValveController::setAuto(index, targetOn);
  publishAutoState(index, targetOn);
}

void handleTimeSet(uint8_t index, const char *payloadStr) {
  char *endPtr = nullptr;
  const long value = strtol(payloadStr, &endPtr, 10);
  if (endPtr == payloadStr || *endPtr != '\0' || value < kMinValveTimeMinutes || value > kMaxValveTimeMinutes) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Wert '%s' fuer V%u/time/set", payloadStr,
                 index);
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

void handleMqttMessage(char *topic, uint8_t *payload, unsigned int length) {
  char payloadStr[8];
  copyPayload(payload, length, payloadStr, sizeof(payloadStr));
  Logger::logf(Logger::Type::SUB, Logger::Source::MQTT, "%s = %s", topic, payloadStr);

  if (strcmp(topic, kMainCmdTopic) == 0) {
    handleMainCmd(payloadStr);
    return;
  }

  uint8_t valveIndex = 0;
  if (parseValveTopic(topic, kCmdSuffix, &valveIndex)) {
    handleValveCmd(valveIndex, payloadStr);
  } else if (parseValveTopic(topic, kTimeSetSuffix, &valveIndex)) {
    handleTimeSet(valveIndex, payloadStr);
  } else if (parseValveTopic(topic, kAutoSetSuffix, &valveIndex)) {
    handleAutoSet(valveIndex, payloadStr);
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
    for (uint8_t i = 1; i <= 5; i++) {
      char cmdTopic[24];
      char timeSetTopic[32];
      char autoSetTopic[32];
      snprintf(cmdTopic, sizeof(cmdTopic), "gartenwasser/V%u/cmd", i);
      snprintf(timeSetTopic, sizeof(timeSetTopic), "gartenwasser/V%u/time/set", i);
      snprintf(autoSetTopic, sizeof(autoSetTopic), "gartenwasser/V%u/auto/set", i);
      mqttClient.subscribe(cmdTopic);
      mqttClient.subscribe(timeSetTopic);
      mqttClient.subscribe(autoSetTopic);
    }
    for (uint8_t i = 0; i < ValveController::kValveCount; i++) {
      publishValveState(i, ValveController::getValve(i));
    }
    for (uint8_t i = 1; i <= 5; i++) {
      publishTimeState(i, ConfigStore::getValveTime(i));
      publishRemaining(i, ValveTimer::getRemainingSeconds(i));
      publishAutoState(i, ValveController::getAuto(i));
    }
    publishMaxTime();
    publishMainState(Sequencer::isRunning());
    publishActiveValve(Sequencer::getActiveValve());
    publishRemainingTotalNow();
  }
  return ok;
}

}  // namespace

void MqttManager::begin() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(handleMqttMessage);
}

void MqttManager::loop() {
  const unsigned long now = millis();
  if (now - lastTickMs >= kTickIntervalMs) {
    lastTickMs = now;
    tickValveTimers();
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
    Logger::log(Logger::Type::ERROR, Logger::Source::MQTT, "Verbindung verloren.");
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
