#include "MqttManager.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cstring>

#include "Logger.h"
#include "ValveController.h"
#include "WifiManager.h"
#include "secrets.h"

namespace {

constexpr const char *kAvailabilityTopic = "gartenwasser/availability";
constexpr const char *kOnlinePayload = "online";
constexpr const char *kOfflinePayload = "offline";
constexpr uint8_t kAvailabilityQos = 1;

constexpr char kValveCmdPrefix[] = "gartenwasser/V";
constexpr char kValveCmdSuffix[] = "/cmd";

// Analog zum Reconnect-Intervall aus WifiManager: kurz genug fuer zuegige
// Wiederholung, lang genug, um den Broker nicht mit Verbindungsversuchen zu fluten.
constexpr unsigned long kReconnectIntervalMs = 15000;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastAttemptMs = 0;
bool wasConnected = false;

// Erkennt Topics der Form "gartenwasser/V{1..5}/cmd" und liefert den Ventil-Index.
bool parseValveCmdTopic(const char *topic, uint8_t *outIndex) {
  const size_t prefixLen = strlen(kValveCmdPrefix);
  if (strncmp(topic, kValveCmdPrefix, prefixLen) != 0) {
    return false;
  }
  const char *rest = topic + prefixLen;
  if (rest[0] < '1' || rest[0] > '5') {
    return false;
  }
  if (strcmp(rest + 1, kValveCmdSuffix) != 0) {
    return false;
  }
  *outIndex = static_cast<uint8_t>(rest[0] - '0');
  return true;
}

void publishValveState(uint8_t index, bool on) {
  char topic[24];
  snprintf(topic, sizeof(topic), "gartenwasser/V%u/state", index);
  mqttClient.publish(topic, on ? "ON" : "OFF", true);
}

// V0-Kopplung: V1-V5 ON schaltet V0 mit ein; V1-V5 OFF schaltet V0 nur aus,
// wenn kein anderes Bewaesserungsventil mehr aktiv ist (siehe docs/requirements.md).
void applyValveCommand(uint8_t index, bool targetOn) {
  if (ValveController::getValve(index) == targetOn) {
    return;  // keine Aenderung, kein State-Update noetig
  }

  ValveController::setValve(index, targetOn);
  publishValveState(index, targetOn);

  if (targetOn) {
    if (!ValveController::getValve(0)) {
      ValveController::setValve(0, true);
      publishValveState(0, true);
    }
  } else if (!ValveController::anyIrrigationValveActive() && ValveController::getValve(0)) {
    ValveController::setValve(0, false);
    publishValveState(0, false);
  }
}

void handleMqttMessage(char *topic, uint8_t *payload, unsigned int length) {
  uint8_t valveIndex = 0;
  if (!parseValveCmdTopic(topic, &valveIndex)) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Unbekanntes Topic: '%s'", topic);
    return;
  }

  char payloadStr[8] = {0};
  const unsigned int copyLen = length < sizeof(payloadStr) - 1 ? length : sizeof(payloadStr) - 1;
  memcpy(payloadStr, payload, copyLen);

  if (strcmp(payloadStr, "ON") == 0) {
    applyValveCommand(valveIndex, true);
  } else if (strcmp(payloadStr, "OFF") == 0) {
    applyValveCommand(valveIndex, false);
  } else {
    Logger::logf(Logger::Type::ERROR, Logger::Source::MQTT, "Ungueltiger Payload '%s' fuer V%u/cmd", payloadStr,
                 valveIndex);
  }
}

bool connectToBroker() {
  const bool ok = mqttClient.connect(MQTT_CLIENT_ID, kAvailabilityTopic, kAvailabilityQos, true, kOfflinePayload);
  if (ok) {
    mqttClient.publish(kAvailabilityTopic, kOnlinePayload, true);
    for (uint8_t i = 1; i <= 5; i++) {
      char topic[24];
      snprintf(topic, sizeof(topic), "gartenwasser/V%u/cmd", i);
      mqttClient.subscribe(topic);
    }
    for (uint8_t i = 0; i < ValveController::kValveCount; i++) {
      publishValveState(i, ValveController::getValve(i));
    }
  }
  return ok;
}

}  // namespace

void MqttManager::begin() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(handleMqttMessage);
}

void MqttManager::loop() {
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
    const unsigned long now = millis();
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
