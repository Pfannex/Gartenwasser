/**
 * @file    WifiManager.h
 * @brief   WLAN-Verbindungsaufbau und nicht-blockierendes Reconnect-Handling.
 */

#pragma once

namespace WifiManager {

/// Startet die WLAN-Verbindung (Zugangsdaten aus secrets.h). Nicht blockierend.
void begin();

/// Muss regelmaessig aus loop() aufgerufen werden (Reconnect bei Verbindungsverlust).
void loop();

/// Liefert true, wenn aktuell eine WLAN-Verbindung besteht.
bool isConnected();

}  // namespace WifiManager
