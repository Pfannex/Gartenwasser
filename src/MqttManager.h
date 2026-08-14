/**
 * @file    MqttManager.h
 * @brief   MQTT-Verbindungsaufbau (PubSubClient), LWT/availability, nicht-blockierendes Reconnect-Handling.
 */

#pragma once

namespace MqttManager {

/// Konfiguriert den MQTT-Client (Broker/Port aus secrets.h). Nicht blockierend.
void begin();

/// Muss regelmaessig aus loop() aufgerufen werden (PubSubClient-Loop, Reconnect bei Verbindungsverlust).
void loop();

/// Liefert true, wenn aktuell eine MQTT-Verbindung besteht.
bool isConnected();

}  // namespace MqttManager
