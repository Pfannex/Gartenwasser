#include "WifiManager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "Logger.h"
#include "secrets.h"

namespace {

// Nur eingreifen, wenn ueber diese Zeit hinweg kein Verbindungsaufbau
// zustande kam. Kurz genug fuer zuegige Wiederholung, lang genug, um den vom
// ESP-IDF WiFi-Treiber laufenden Verbindungsversuch nicht zu unterbrechen
// (ein zu frueher erneuter WiFi.begin() fuehrt sonst zu
// "wifi:sta is connecting, return error").
constexpr unsigned long kReconnectIntervalMs = 15000;

unsigned long lastAttemptMs = 0;
bool wasConnected = false;

}  // namespace

void WifiManager::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);  // ESP-IDF haengt sich nach Verbindungsverlust selbst wieder ein
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastAttemptMs = millis();
  Logger::logf(Logger::Type::INFO, Logger::Source::WIFI, "Verbinde mit '%s' ...", WIFI_SSID);
}

void WifiManager::loop() {
  const bool connected = isConnected();

  if (connected && !wasConnected) {
    Logger::logf(Logger::Type::INFO, Logger::Source::WIFI, "Verbunden. IP: %s",
                 WiFi.localIP().toString().c_str());
  } else if (!connected && wasConnected) {
    Logger::log(Logger::Type::ERROR, Logger::Source::WIFI, "Verbindung verloren.");
  }
  wasConnected = connected;

  if (!connected) {
    const unsigned long now = millis();
    if (now - lastAttemptMs >= kReconnectIntervalMs) {
      lastAttemptMs = now;
      const wl_status_t status = WiFi.status();
      // Nur bei eindeutig abgeschlossenem Fehlversuch neu starten - nicht,
      // waehrend der Treiber noch verbindet (WL_IDLE_STATUS).
      if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL || status == WL_DISCONNECTED) {
        Logger::log(Logger::Type::INFO, Logger::Source::WIFI, "Reconnect-Versuch ...");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }
    }
  }
}

bool WifiManager::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}
