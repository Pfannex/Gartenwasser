/**
 * @file    WebManager.h
 * @brief   Async-Webserver (ESPAsyncWebServer), liefert das Web-Interface als statische
 *          Dateien aus LittleFS aus (Phase 16). Reines File-Serving - keine REST-Endpoints:
 *          der Browser spricht fuer Live-Daten direkt per MQTT-over-WebSocket mit dem
 *          Broker, siehe docs/spec/16-webif-fundament.md (Architekturentscheidung B).
 */

#pragma once

class WebManager {
 public:
  WebManager() = delete;

  /// Startet den Webserver. Setzt voraus, dass LittleFS bereits gemountet ist
  /// (siehe ConfigStore::begin(), muss vorher aufgerufen werden).
  static void begin();
};
