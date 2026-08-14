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
#include "WifiManager.h"

void setup() {
  Serial.begin(115200);
  delay(500);

  // WLAN-Verbindung starten (nicht blockierend, siehe WifiManager::loop())
  WifiManager::begin();

  // Display/Touch/LVGL initialisieren (startet dabei auch den I2C-Bus)
  HmiManager::begin();

  // I2C-Bus scannen und MCP23017 vorbereiten
  I2CManager::scan();
  I2CManager::mcp23017Setup();

  Serial.println("Setup fertig!");
}

void loop() {
  WifiManager::loop();
  HmiManager::loop();
}
