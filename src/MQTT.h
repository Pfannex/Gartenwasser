/**
 * @file    MQTT.h
 * @brief   MQTT-Verbindungsaufbau (PubSubClient), LWT/availability, nicht-blockierendes Reconnect-Handling.
 */

#pragma once

#include <cstdint>

namespace MQTT {

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

/// Wendet Programm `programIndex` (1-basiert, 0 = keins/abwaehlen) an - identisch zu
/// main/program/cmd, aber ohne MQTT-Umweg. Fuer die Touch-UI-Programme-Unterseite
/// (Touch-UI-Neugestaltung, siehe docs/spec/13-touch-ui.md), die per </>-Buttons
/// durch alle Programme blaettert.
void requestProgramSelect(uint8_t programIndex);

/// Schaltet Ventil `index` (1..5) direkt (identisch V{n}/cmd MQTT-Befehl), ohne
/// MQTT-Umweg - fuer die Touch-UI-Ventilmatrix (Touch-UI-Neugestaltung, siehe
/// docs/spec/13-touch-ui.md).
void requestValveCmd(uint8_t index, bool on);

}  // namespace MQTT
