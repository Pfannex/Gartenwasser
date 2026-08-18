/**
 * @file    OTA.h
 * @brief   PlatformIO/VSCode-OTA (Phase 21, zweiter Teil) - `ArduinoOTA`-Bibliothek,
 *          ermoeglicht `pio run --target upload`/`uploadfs` uebers WLAN statt Kabel
 *          (eigener Dev-Workflow). Unabhaengig vom WebIF-Browser-Upload (siehe WebIF.h),
 *          der fuer Nutzer ohne Entwicklungsumgebung gedacht ist.
 */

#pragma once

class OTA {
 public:
  OTA() = delete;

  /// Registriert Callbacks und startet den ArduinoOTA-Dienst (inkl. mDNS-Ankuendigung als
  /// "gartenwasser.local"). Setzt eine bestehende WLAN-Verbindung voraus.
  static void begin();

  /// Muss regelmaessig aus main.cpp::loop() aufgerufen werden.
  static void loop();
};
