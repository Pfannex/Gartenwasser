#include "WiFiController.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "Logger.h"
#include "secrets.h"

namespace {

// Nur eingreifen, wenn ueber diese Zeit hinweg kein Verbindungsaufbau
// zustande kam. Kurz genug fuer zuegige Wiederholung, lang genug, um den vom
// ESP-IDF WiFi-Treiber laufenden Verbindungsversuch nicht zu unterbrechen
// (ein zu frueher erneuter WiFi.begin() fuehrt sonst zu
// "wifi:sta is connecting, return error").
constexpr unsigned long kReconnectIntervalMs = 15000;

// Timeouts fuer den blockierenden Boot-Ablauf (connectAndSyncTimeBlocking()).
constexpr unsigned long kWifiConnectTimeoutMs = 20000;
constexpr unsigned long kNtpSyncTimeoutMs = 10000;
constexpr const char *kTimezone = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr const char *kNtpServer1 = "pool.ntp.org";
constexpr const char *kNtpServer2 = "de.pool.ntp.org";
// Alles darunter gilt als "noch nicht synchronisiert" (Systemuhr startet 1970).
constexpr time_t kMinValidEpoch = 1700000000;

unsigned long lastAttemptMs = 0;
bool wasConnected = false;

// Blockiert, bis WLAN verbunden ist oder das Timeout erreicht wird.
bool waitForConnection(unsigned long timeoutMs) {
  const unsigned long start = millis();
  while (!WiFiController::isConnected()) {
    if (millis() - start >= timeoutMs) {
      return false;
    }
    WiFiController::loop();
    delay(100);
  }
  return true;
}

// Blockiert, bis die Systemzeit per NTP synchronisiert ist oder das Timeout erreicht wird.
bool waitForNtpSync(unsigned long timeoutMs) {
  configTzTime(kTimezone, kNtpServer1, kNtpServer2);
  const unsigned long start = millis();
  while (time(nullptr) < kMinValidEpoch) {
    if (millis() - start >= timeoutMs) {
      return false;
    }
    delay(100);
  }
  return true;
}

}  // namespace

void WiFiController::begin() {
  WiFi.mode(WIFI_STA);
  // Muss vor WiFi.begin() gesetzt werden, um zu greifen - macht das Geraet im Router/DHCP
  // erkennbar und ist der mDNS-Name, unter dem OTA::begin() (Phase 21, PlatformIO-OTA) das
  // Geraet als "gartenwasser.local" bewirbt.
  WiFi.setHostname("gartenwasser");
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);  // ESP-IDF haengt sich nach Verbindungsverlust selbst wieder ein
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastAttemptMs = millis();
  Logger::logf(Logger::Type::INFO, Logger::Source::WIFI, "Verbinde mit '%s' ...", WIFI_SSID);
}

void WiFiController::loop() {
  const bool connected = isConnected();

  if (connected && !wasConnected) {
    Logger::logf(Logger::Type::INFO, Logger::Source::WIFI, "Verbunden. IP: %s",
                 WiFi.localIP().toString().c_str());
  } else if (!connected && wasConnected) {
    Logger::log(Logger::Type::ERROR, Logger::Source::WIFI, "WLAN Verbindung verloren.");
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

bool WiFiController::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void WiFiController::connectAndSyncTimeBlocking() {
  begin();
  if (waitForConnection(kWifiConnectTimeoutMs)) {
    if (waitForNtpSync(kNtpSyncTimeoutMs)) {
      Logger::enableRealTime();
      Logger::log(Logger::Type::INFO, Logger::Source::SYSTEM, "NTP-Synchronisierung erfolgreich.");
    } else {
      Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "NTP-Synchronisierung fehlgeschlagen (Timeout).");
    }
  } else {
    Logger::log(Logger::Type::ERROR, Logger::Source::SYSTEM, "WLAN-Verbindung fehlgeschlagen (Timeout).");
  }
}
