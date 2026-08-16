/**
 * @file    main.cpp
 * @brief   Projekt "Gartenwasser" - Einstiegspunkt (Setup/Loop-Orchestrierung).
 *
 * Die eigentliche Logik steckt in eigenen Klassen (src/), main.cpp bleibt ein
 * schlanker Orchestrator: WifiManager (WLAN/NTP), MqttManager (MQTT-Verbindung
 * und komplette Ventil-/Automatik-/Config-Anbindung), HmiManager (Display/
 * Touch/LVGL), I2CManager (I2C-Bus, MCP23017), ValveController (Ventil-Ein-
 * gänge/Alias), ValveTimer (Restlaufzeit), Sequencer (Automatik-Ablauf),
 * ConfigStore (Persistenz), Diagnostics (i2cStatus/lastError), Logger.
 */

#include <Arduino.h>

#include "ConfigStore.h"
#include "Diagnostics.h"
#include "HmiManager.h"
#include "I2CManager.h"
#include "MqttManager.h"
#include "Sequencer.h"
#include "ValveController.h"
#include "ValveTimer.h"
#include "WifiManager.h"
#include "Logger.h"

// Default-Stack der Arduino-loopTask (8192 Byte) reicht nicht mehr: main/programs/set
// (Phase 14) verarbeitet mehrere StaticJsonDocument<2048>/Puffer gleichzeitig auf dem
// Stack (handleMqttMessage -> handleProgramsSet), das fuehrte zu einem "Stack protection
// fault" (Guru Meditation Error). RAM-Headroom ist reichlich vorhanden (siehe Log.md,
// Speicher-Check Phase 12), daher grosszuegig verdoppelt statt die JSON-Puffer zu verkleinern.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
  Serial.begin(115200);
  delay(500);

  // Boot-Trenner, damit im Serial-Monitor klar erkennbar ist, wo ein Neustart beginnt.
  Serial.println();
  Serial.println("------------------------------------------------------------");

  // So frueh wie moeglich, damit auch fruehe Boot-Fehler (SPIFFS, WLAN, ...)
  // in diagnostics/lastError landen (siehe Logger::setErrorCallback()).
  Diagnostics::begin();

  // Persistente Konfiguration laden (time, auto, alias, maxTime)
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
  Sequencer::begin();

  Logger::log(Logger::Type::INFO, Logger::Source::SYSTEM, "Setup abgeschlossen.");
}

void loop() {
  WifiManager::loop();
  MqttManager::loop();
  HmiManager::loop();
}
