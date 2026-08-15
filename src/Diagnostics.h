/**
 * @file    Diagnostics.h
 * @brief   I2C-Bus-Status und letzte Fehlermeldung, fuer diagnostics/i2cStatus
 *          und diagnostics/lastError. Registriert sich als Logger::ErrorCallback,
 *          damit jeder ERROR-Log-Eintrag (I2C-Ausfall, ungueltige MQTT-Payloads,
 *          WLAN-Verlust, ...) automatisch in lastError landet.
 */

#pragma once

class Diagnostics {
 public:
  Diagnostics() = delete;

  /// Initialisiert den Zustand und registriert den Logger::ErrorCallback.
  static void begin();

  /// Prueft den I2C-Bus (MCP23017) und aktualisiert den internen Status.
  /// Liefert true, wenn sich der Status seit dem letzten Aufruf geaendert hat.
  static bool checkI2cStatus();

  /// true = i2cStatus "ok", false = "error".
  static bool isI2cOk();

  /// Liefert true (und setzt intern zurueck), wenn seit dem letzten Abruf ein
  /// neuer Fehler aufgetreten ist - fuer die Publish-Seite in MqttManager.
  static bool consumeNewError();

  /// Zuletzt aufgetretene Fehlermeldung inkl. Zeitstempel, leer wenn noch keine.
  static const char *getLastError();
};
