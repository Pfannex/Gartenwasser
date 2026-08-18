/**
 * @file    WiFiController.h
 * @brief   WLAN-Verbindungsaufbau und nicht-blockierendes Reconnect-Handling.
 */

#pragma once

namespace WiFiController {

/// Startet die WLAN-Verbindung (Zugangsdaten aus secrets.h). Nicht blockierend.
void begin();

/// Muss regelmaessig aus loop() aufgerufen werden (Reconnect bei Verbindungsverlust).
void loop();

/// Liefert true, wenn aktuell eine WLAN-Verbindung besteht.
bool isConnected();

/// Blockiert beim Boot: baut die WLAN-Verbindung auf und synchronisiert danach
/// die Systemzeit per NTP (je mit Timeout). Schaltet den Logger bei Erfolg auf
/// Echtzeit um (siehe Logger::enableRealTime()).
void connectAndSyncTimeBlocking();

}  // namespace WiFiController
