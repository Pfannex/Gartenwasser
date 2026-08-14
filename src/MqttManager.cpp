#include "MqttManager.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cstdlib>
#include <cstring>

#include "ConfigStore.h"
#include "Logger.h"
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

constexpr char kValvePrefix[] = "gartenwasser/V";
constexpr char kCmdSuffix[] = "/cmd";
constexpr char kTimeSetSuffix[] = "/time/set";

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

unsigned long lastAttemptMs = 0;
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

void publishValveState(uint8_t index, bool on) {
  char topic[24];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/state", index);
  mqttClient.publish(topic, on ? "ON" : "OFF", true);
}

void publishTimeState(uint8_t index, uint16_t minutes) {
  char topic[32];
  char payload[8];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/time/state", index);
  snprintf(payload, sizeof(payload), "%u", minutes);
  mqttClient.publish(topic, payload, true);
}

void publishRemaining(uint8_t index, uint16_t seconds) {
  char topic[32];
  char payload[8];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/time/remaining", index);
  snprintf(payload, sizeof(payload), "%02u:%02u", seconds / 60, seconds % 60);
  mqttClient.publish(topic, payload);  // nicht retained (sekuendlicher Live-Wert)
}

void publishMaxTime() {
  char payload[8];
  snprintf(payload, sizeof(payload), "%u", ConfigStore::getMaxTime());
  mqttClient.publish(kMaxTimeTopic, payload, true);
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
    ValveTimer::stop(index);
    publishRemaining(index, 0);
    if (!ValveController::anyIrrigationValveActive() && ValveController::getValve(0)) {
      ValveController::setValve(0, false);
      publishValveState(0, false);
    }
  }
}

void handleValveCmd(uint8_t index, const char *payloadStr) {
  if (strcmp(payloadStr, "ON") == 0) {
    applyValveCommand(index, true);
  } else if (strcmp(payloadStr, "OFF") == 0) {
    applyValveCommand(index, false);
  } else {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Payload '%s' fuer V%u/cmd", payloadStr,
                 index);
  }
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
}

void handleMqttMessage(char *topic, uint8_t *payload, unsigned int length) {
  char payloadStr[8];
  copyPayload(payload, length, payloadStr, sizeof(payloadStr));

  uint8_t valveIndex = 0;
  if (parseValveTopic(topic, kCmdSuffix, &valveIndex)) {
    handleValveCmd(valveIndex, payloadStr);
  } else if (parseValveTopic(topic, kTimeSetSuffix, &valveIndex)) {
    handleTimeSet(valveIndex, payloadStr);
  } else {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Unbekanntes Topic: '%s'", topic);
  }
}

// Sicherheitskritisch: laeuft unabhaengig von WLAN/MQTT-Verbindungsstatus (siehe
// Aufrufstelle in MqttManager::loop()). publish()-Aufrufe sind bei fehlender
// Verbindung ein sicherer No-Op (PubSubClient liefert dann nur false zurueck).
void tickValveTimers() {
  const uint8_t expiredMask = ValveTimer::tick();
  for (uint8_t i = 1; i <= 5; i++) {
    if (ValveController::getValve(i)) {
      publishRemaining(i, ValveTimer::getRemainingSeconds(i));
    }
    if (expiredMask & (1 << i)) {
      applyValveCommand(i, false);
    }
  }
}

bool connectToBroker() {
  const bool ok = mqttClient.connect(MQTT_CLIENT_ID, kAvailabilityTopic, kAvailabilityQos, true, kOfflinePayload);
  if (ok) {
    mqttClient.publish(kAvailabilityTopic, kOnlinePayload, true);
    for (uint8_t i = 1; i <= 5; i++) {
      char cmdTopic[24];
      char timeSetTopic[32];
      snprintf(cmdTopic, sizeof(cmdTopic), "gartenwasser/V%u/cmd", i);
      snprintf(timeSetTopic, sizeof(timeSetTopic), "gartenwasser/V%u/time/set", i);
      mqttClient.subscribe(cmdTopic);
      mqttClient.subscribe(timeSetTopic);
    }
    for (uint8_t i = 0; i < ValveController::kValveCount; i++) {
      publishValveState(i, ValveController::getValve(i));
    }
    for (uint8_t i = 1; i <= 5; i++) {
      publishTimeState(i, ConfigStore::getValveTime(i));
      publishRemaining(i, ValveTimer::getRemainingSeconds(i));
    }
    publishMaxTime();
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

  if (connected && !wasConnected) {
    Logger::log(Logger::Type::INFO, Logger::Source::MQTT, "Verbunden.");
  } else if (!connected && wasConnected) {
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
