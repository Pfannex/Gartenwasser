/**
 * @file    MqttManager.h
 * @brief   MQTT-Verbindungsaufbau (PubSubClient), LWT/availability, nicht-blockierendes Reconnect-Handling.
 */

#pragma once

#include <cstdint>

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

/// Wendet das Programm an, dessen "shortcut"-Feld dem physischen Button `shortcut`
/// entspricht (1..4 fuer P1..P4) - fuer die Touch-UI-Buttons (Phase 13/14), kein
/// MQTT-Umweg. Kein Effekt (nur Log-Eintrag), wenn kein Programm diesen Shortcut hat.
void requestProgramByShortcut(uint8_t shortcut);

/// Loescht die Programm-Auswahl (identisch zu main/program/cmd 0) - fasst keine Ventile
/// an. Fuer die Touch-UI: erneutes Druecken eines bereits aktiven Programm-Buttons.
void requestProgramClear();

}  // namespace MqttManager
