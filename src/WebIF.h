/**
 * @file    WebIF.h
 * @brief   Async-Webserver (ESPAsyncWebServer), liefert das Web-Interface als statische
 *          Dateien aus LittleFS aus (Phase 16). Fuer Live-Daten spricht der Browser direkt
 *          per MQTT-over-WebSocket mit dem Broker, siehe docs/spec/16-webif-fundament.md
 *          (Architekturentscheidung B). Ausnahme (Phase 21, OTA): zwei echte HTTP-POST-
 *          Upload-Endpunkte fuer Firmware/Dateisystem - MQTT-Payloads sind bei uns auf
 *          wenige KB gedeckelt, fuer mehrere-MB-Binaries bewusst kein Ersatz fuer echten
 *          HTTP-Upload.
 */

#pragma once

class WebIF {
 public:
  WebIF() = delete;

  /// Startet den Webserver. Setzt voraus, dass LittleFS bereits gemountet ist
  /// (siehe FileSystem::begin(), muss vorher aufgerufen werden).
  static void begin();

  /// Muss regelmaessig aus main.cpp::loop() aufgerufen werden - erledigt aktuell nur den
  /// verzoegerten Neustart nach einem erfolgreichen OTA-Update (siehe WebIF.cpp), damit die
  /// HTTP-Erfolgsantwort dem Browser sicher noch zugestellt wird, bevor ESP.restart() greift.
  static void loop();
};
