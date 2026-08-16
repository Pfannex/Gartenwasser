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

/// Startet/stoppt die Automatik-Sequenz genau wie ein main/cmd MQTT-Befehl
/// (inkl. aller Publishes) - fuer lokale Ausloeser wie das Touch-UI, keine
/// Sonderlogik gegenueber dem MQTT-Pfad.
void requestMainCmd(bool on);

}  // namespace MqttManager
