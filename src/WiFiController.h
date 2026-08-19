/**
 * @file    WiFiController.h
 * @brief   WLAN-Verbindungsaufbau und nicht-blockierendes Reconnect-Handling.
 */

#pragma once

namespace WiFiController {

using ReconnectCallback = void (*)();

/// Startet die WLAN-Verbindung (Zugangsdaten aus secrets.h). Nicht blockierend.
void begin();

/// Registriert einen Callback, der aus loop() heraus bei jedem (Wieder-)Verbindungsaufbau
/// aufgerufen wird. Der allererste Boot-Verbindungsaufbau passiert blockierend in
/// connectAndSyncTimeBlocking(), also VOR jeder Callback-Registrierung - der Callback feuert
/// in der Praxis daher nur bei echten Reconnects nach einem WLAN-Abbruch, nicht beim Boot.
/// Fuer Module wie WebIF, die nach einem WLAN-Reconnect selbst wieder aktiv werden muessen
/// (siehe WebIF::begin()), ohne dass WiFiController WebIF kennen muss (gleiches Muster wie
/// Logger::setErrorCallback()).
void setReconnectCallback(ReconnectCallback callback);

/// Muss regelmaessig aus loop() aufgerufen werden (Reconnect bei Verbindungsverlust).
void loop();

/// Liefert true, wenn aktuell eine WLAN-Verbindung besteht.
bool isConnected();

/// Blockiert beim Boot: baut die WLAN-Verbindung auf und synchronisiert danach
/// die Systemzeit per NTP (je mit Timeout). Schaltet den Logger bei Erfolg auf
/// Echtzeit um (siehe Logger::enableRealTime()).
void connectAndSyncTimeBlocking();

}  // namespace WiFiController
