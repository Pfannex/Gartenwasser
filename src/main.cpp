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

#include "HmiManager.h"
#include "I2CManager.h"
#include "MqttManager.h"
#include "WifiManager.h"
#include "Logger.h"

void setup() {
  Serial.begin(115200);
  delay(500);

  // WLAN verbinden, danach NTP synchronisieren (beides blockierend beim Boot, siehe WifiManager).
  WifiManager::connectAndSyncTimeBlocking();

  // MQTT-Client konfigurieren (nicht blockierend, siehe MqttManager::loop())
  MqttManager::begin();

  // Display/Touch/LVGL initialisieren (startet dabei auch den I2C-Bus)
  HmiManager::begin();

  // I2C-Bus scannen und MCP23017 vorbereiten
  I2CManager::scan();
  I2CManager::mcp23017Setup();

  Logger::log(Logger::Type::INFO, Logger::Source::SYSTEM, "Setup abgeschlossen.");
}

void loop() {
  WifiManager::loop();
  MqttManager::loop();
  HmiManager::loop();
}
