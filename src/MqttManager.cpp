#include "MqttManager.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "Logger.h"
#include "WifiManager.h"
#include "secrets.h"

namespace {

constexpr const char *kAvailabilityTopic = "gartenwasser/availability";
constexpr const char *kOnlinePayload = "online";
constexpr const char *kOfflinePayload = "offline";
constexpr uint8_t kAvailabilityQos = 1;

// Analog zum Reconnect-Intervall aus WifiManager: kurz genug fuer zuegige
// Wiederholung, lang genug, um den Broker nicht mit Verbindungsversuchen zu fluten.
constexpr unsigned long kReconnectIntervalMs = 15000;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastAttemptMs = 0;
bool wasConnected = false;

bool connectToBroker() {
  const bool ok = mqttClient.connect(MQTT_CLIENT_ID, kAvailabilityTopic, kAvailabilityQos, true, kOfflinePayload);
  if (ok) {
    mqttClient.publish(kAvailabilityTopic, kOnlinePayload, true);
  }
  return ok;
}

}  // namespace

void MqttManager::begin() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
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
