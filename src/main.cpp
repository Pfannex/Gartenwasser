/**
 * @file    main.cpp
 * @brief   Projekt "Gartenwasser" - Einstiegspunkt (Setup/Loop-Orchestrierung).
 *
 * Die eigentliche Hardware-/Netzwerklogik steckt in eigenen Klassen:
 * WifiManager (WLAN), HmiManager (Display/Touch/LVGL), I2CManager (I2C-Bus,
 * MCP23017). Die Anwendungslogik (Bewässerungssteuerung) wird im Anschluss
 * gemeinsam spezifiziert und hier ergänzt.
 */

#include <Arduino.h>

#include "ConfigStore.h"
#include "HmiManager.h"
#include "I2CManager.h"
#include "MqttManager.h"
#include "ValveController.h"
#include "ValveTimer.h"
#include "WifiManager.h"
#include "Logger.h"

void setup() {
  Serial.begin(115200);
  delay(500);

  // Boot-Trenner, damit im Serial-Monitor klar erkennbar ist, wo ein Neustart beginnt.
  Serial.println();
  Serial.println("------------------------------------------------------------");

  // Persistente Konfiguration laden (time, maxTime; spaeter auto, alias)
  ConfigStore::begin();

  // WLAN verbinden, danach NTP synchronisieren (beides blockierend beim Boot, siehe WifiManager).
  WifiManager::connectAndSyncTimeBlocking();

  // MQTT-Client konfigurieren (nicht blockierend, siehe MqttManager::loop())
  MqttManager::begin();

  // Display/Touch/LVGL initialisieren (startet dabei auch den I2C-Bus)
  HmiManager::begin();

  // I2C-Bus scannen und MCP23017 vorbereiten
  I2CManager::scan();
  I2CManager::mcp23017Setup();

  // Ventile V0-V5 auf sicheren Grundzustand (AUS) setzen
  ValveController::begin();
  ValveTimer::begin();

  Logger::log(Logger::Type::INFO, Logger::Source::SYSTEM, "Setup abgeschlossen.");
}

void loop() {
  WifiManager::loop();
  MqttManager::loop();
  HmiManager::loop();
}
