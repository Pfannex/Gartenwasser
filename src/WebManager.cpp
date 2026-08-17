#include "WebManager.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "Logger.h"

namespace {

constexpr uint16_t kHttpPort = 80;
AsyncWebServer server(kHttpPort);

}  // namespace

void WebManager::begin() {
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); });
  server.begin();
  Logger::log(Logger::Type::INFO, Logger::Source::WEB, "WebManager: Webserver gestartet (Port 80).");
}
