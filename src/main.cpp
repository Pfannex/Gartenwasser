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
#include "ValveController.h"
#include "WifiManager.h"
#include "Logger.h"

namespace {

// Temporärer Test-Handler für Phase 3 (ValveController ohne MQTT-Anbindung).
// Befehle über Serial: "v0on".."v5on" / "v0off".."v5off". Entfällt mit Phase 4.
void handleValveTestCommand() {
  if (!Serial.available()) {
    return;
  }
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) {
    return;
  }

  for (uint8_t i = 0; i < ValveController::kValveCount; i++) {
    char onCmd[8];
    char offCmd[8];
    snprintf(onCmd, sizeof(onCmd), "v%uon", i);
    snprintf(offCmd, sizeof(offCmd), "v%uoff", i);
    if (line.equalsIgnoreCase(onCmd)) {
      ValveController::setValve(i, true);
      return;
    }
    if (line.equalsIgnoreCase(offCmd)) {
      ValveController::setValve(i, false);
      return;
    }
  }
  Logger::logf(Logger::Type::ERROR, Logger::Source::SYSTEM, "Unbekannter Test-Befehl: '%s'", line.c_str());
}

}  // namespace

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

  // Ventile V0-V5 auf sicheren Grundzustand (AUS) setzen
  ValveController::begin();

  Logger::log(Logger::Type::INFO, Logger::Source::SYSTEM, "Setup abgeschlossen.");
}

void loop() {
  WifiManager::loop();
  MqttManager::loop();
  HmiManager::loop();
  handleValveTestCommand();
}
