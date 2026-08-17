#include "WebManager.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "Logger.h"

namespace {

constexpr uint16_t kHttpPort = 80;
AsyncWebServer server(kHttpPort);

}  // namespace

void WebManager::begin() {
  // Eigene Partition "webfs" (siehe partitions.csv), getrennt von ConfigStores "config"-
  // Partition - genau die Partition, die `pio run --target uploadfs` bespielt. formatOnFail
  // bewusst false: auf Hardware verifiziert, dass ein gueltiges uploadfs-Image mit
  // formatOnFail=true faelschlich als ungueltig erkannt und automatisch neu formatiert
  // wurde (siehe docs/spec/16-webif-fundament.md).
  if (!LittleFS.begin(false, "/littlefs", 10, "webfs")) {
    Logger::log(Logger::Type::ERROR, Logger::Source::WEB, "WebManager: LittleFS-Mount (webfs) fehlgeschlagen.");
    return;
  }

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); });
  server.begin();
  Logger::log(Logger::Type::INFO, Logger::Source::WEB, "WebManager: Webserver gestartet (Port 80).");
}
