#include "WebIF.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>

#include "Logger.h"
#include "WiFiController.h"

namespace {

constexpr uint16_t kHttpPort = 80;
AsyncWebServer server(kHttpPort);

// Verzoegerter Neustart nach OTA-Erfolg (siehe WebIF::loop()) statt ESP.restart() direkt im
// Upload-Callback - sonst besteht das Risiko, dass die HTTP-Erfolgsantwort den Browser nicht
// mehr erreicht, weil die Verbindung durch den Neustart schon weg ist, bevor AsyncTCP sie
// tatsaechlich rausgeschickt hat.
bool restartRequested = false;
unsigned long restartRequestedAtMs = 0;
constexpr unsigned long kRestartDelayMs = 500;

// Zwischen index==0 (Update.begin()) und final==true (Update.end()) kann kein Fehler ueber
// den Rueckgabewert von onUpload() gemeldet werden (Callback hat keinen Rueckgabewert) -
// daher als Flag gemerkt und vom zugehoerigen Request-Handler ausgewertet.
bool otaFailed = false;

// Gemeinsame Kernlogik fuer beide Upload-Ziele (U_FLASH=Firmware/app-Partition,
// U_SPIFFS=Dateisystem/"webfs"-Partition, siehe partitions.csv - Update sucht bei U_SPIFFS
// automatisch die Partition mit SubType "spiffs", die "config"-Partition hat einen anderen
// SubType und wird dadurch nie getroffen, bleibt also von OTA-Updates unberuehrt).
void handleOtaChunk(int command, const char *label, size_t index, uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    otaFailed = false;
    Logger::logf(Logger::Type::INFO, Logger::Source::OTA, "%s-Upload gestartet.", label);
    // Dateisystem-Ziel ("webfs") ist waehrend des laufenden Betriebs gemountet (siehe
    // FileSystem::begin()/WebIF::begin()) - vor dem rohen Ueberschreiben der Partition
    // aushaengen, sonst droht ein inkonsistenter Zustand. Neu gemountet wird erst wieder
    // nach dem Neustart, ein sofortiges Remount ist hier nicht noetig.
    if (command == U_SPIFFS) {
      LittleFS.end();
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
      Logger::logf(Logger::Type::ERROR, Logger::Source::OTA, "%s: Update.begin() fehlgeschlagen: %s", label,
                   Update.errorString());
      otaFailed = true;
      return;
    }
  }
  if (otaFailed) {
    return;
  }
  if (len > 0 && Update.write(data, len) != len) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::OTA, "%s: Update.write() fehlgeschlagen: %s", label,
                 Update.errorString());
    otaFailed = true;
    return;
  }
  if (final) {
    if (Update.end(true)) {
      Logger::logf(Logger::Type::INFO, Logger::Source::OTA, "%s-Upload abgeschlossen (%u Byte). MD5=%s", label,
                   static_cast<unsigned>(index + len), Update.md5String().c_str());
    } else {
      Logger::logf(Logger::Type::ERROR, Logger::Source::OTA, "%s: Update.end() fehlgeschlagen: %s", label,
                   Update.errorString());
      otaFailed = true;
    }
  }
}

// ESPAsyncWebServer::begin() wird nur einmal im Setup aufgerufen (siehe WebIF::begin()) - nach
// einem WLAN-Verbindungsabbruch+Wiederverbindung (neues Netzwerkinterface/neue DHCP-Lease
// intern) kann der zugrundeliegende TCP-Listen-Socket ungueltig werden, ohne dass der Server
// das selbst bemerkt: er "denkt" er lauscht noch, nimmt aber keine neuen Verbindungen mehr an
// (bekanntes ESPAsyncWebServer/LWIP-Verhalten). MQTT ist davon nicht betroffen, weil
// MqttManager::loop() bei Verbindungsverlust aktiv einen komplett neuen Socket aufbaut - WebIF
// hatte bislang kein Aequivalent dazu. Fix: bei jedem von WiFiController gemeldeten Reconnect
// (siehe WiFiController::setReconnectCallback() in WebIF::begin() unten) den Server explizit
// neu binden. server.end()/begin() loescht dabei NICHT die per server.on(...) registrierten
// Routen (die bleiben im AsyncWebServer-Objekt bestehen) - nur der Listen-Socket wird neu
// aufgebaut.
void rebindServer() {
  server.end();
  server.begin();
  Logger::log(Logger::Type::INFO, Logger::Source::WEB, "WebIF: Server nach WLAN-Reconnect neu gebunden.");
}

void handleOtaResult(AsyncWebServerRequest *request) {
  const bool ok = !otaFailed;
  AsyncWebServerResponse *response =
      request->beginResponse(ok ? 200 : 500, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
  response->addHeader("Connection", "close");
  request->send(response);
  if (ok) {
    restartRequested = true;
    restartRequestedAtMs = millis();
  }
}

}  // namespace

void WebIF::begin() {
  // Eigene Partition "webfs" (siehe partitions.csv), getrennt von FileSystems "config"-
  // Partition - genau die Partition, die `pio run --target uploadfs` bespielt. formatOnFail
  // bewusst false: auf Hardware verifiziert, dass ein gueltiges uploadfs-Image mit
  // formatOnFail=true faelschlich als ungueltig erkannt und automatisch neu formatiert
  // wurde (siehe docs/spec/16-webif-fundament.md).
  if (!LittleFS.begin(false, "/littlefs", 10, "webfs")) {
    Logger::log(Logger::Type::ERROR, Logger::Source::WEB, "WebIF: LittleFS-Mount (webfs) fehlgeschlagen.");
    return;
  }

  // No-Cache waehrend der aktiven Entwicklung (2026-08-17, Nutzer-Wunsch nach einem
  // Browser-Cache-Missverstaendnis auf dem Handy): jede Aenderung soll sofort sichtbar
  // sein, statt gegen veraltete gecachte Dateien zu testen. Vor einem produktiven Release
  // wieder auf normales Caching umstellen (die Dateien aendern sich dann nicht mehr staendig).
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache, no-store, must-revalidate");
  // DEBUG statt INFO/ERROR: faellt beim Browsen routinemaessig auch fuer harmlose Anfragen
  // wie /favicon.ico an, ist aber fuers Aufspueren kaputter/veralteter Links im Web-Interface
  // nuetzlich (Nachtrag 2026-08-18, Logging-Brainstorming).
  server.onNotFound([](AsyncWebServerRequest *request) {
    Logger::logf(Logger::Type::DEBUG, Logger::Source::WEB, "404: %s", request->url().c_str());
    request->send(404, "text/plain", "Not found");
  });

  // OTA-Upload (Phase 21): zwei getrennte Ziele statt eines gebuendelten Archivs (Nutzer-
  // Entscheidung) - PlatformIO erzeugt Firmware und Dateisystem ohnehin als zwei separate
  // Dateien, und beide lassen sich damit unabhaengig voneinander aktualisieren (genau wie
  // wir es waehrend der Entwicklung staendig per `pio run -t upload`/`-t uploadfs` einzeln tun).
  server.on(
      "/api/ota/firmware", HTTP_POST, handleOtaResult,
      [](AsyncWebServerRequest *, const String &, size_t index, uint8_t *data, size_t len, bool final) {
        handleOtaChunk(U_FLASH, "Firmware", index, data, len, final);
      });
  server.on(
      "/api/ota/filesystem", HTTP_POST, handleOtaResult,
      [](AsyncWebServerRequest *, const String &, size_t index, uint8_t *data, size_t len, bool final) {
        handleOtaChunk(U_SPIFFS, "Dateisystem", index, data, len, final);
      });

  server.begin();
  Logger::log(Logger::Type::INFO, Logger::Source::WEB, "WebIF: Webserver gestartet (Port 80).");

  WiFiController::setReconnectCallback(rebindServer);
}

void WebIF::loop() {
  if (restartRequested && millis() - restartRequestedAtMs >= kRestartDelayMs) {
    restartRequested = false;
    Logger::log(Logger::Type::INFO, Logger::Source::SYSTEM, "Neustart nach OTA-Update ...");
    ESP.restart();
  }
}
