# Phase 1 — WLAN-Verbindung

**Status:** ✅ Erledigt & getestet

## Ziel

Zuverlässige, nicht-blockierende WLAN-Verbindung mit automatischem Reconnect.

## Umsetzung

- `WifiManager` (`src/WifiManager.h/.cpp`): `begin()` startet die Verbindung, `loop()` prüft Status und stößt bei eindeutig gescheitertem Verbindungsversuch (`WL_CONNECT_FAILED`, `WL_NO_SSID_AVAIL`, `WL_DISCONNECTED`) einen Reconnect an. `WiFi.setAutoReconnect(true)` übernimmt die Wiederverbindung nach Verbindungsverlust.
- Nutzt `Logger` (Source `WIFI`) für alle Statusmeldungen.

## Betroffene Dateien

- `src/WifiManager.h`, `src/WifiManager.cpp`
- `src/main.cpp` (Einbindung in `setup()`/`loop()`)

## Gefundene Probleme & Fixes (während der Umsetzung)

1. **ESP32-C6 USB-Serial/JTAG statt externem USB-UART-Chip**: Ohne `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` in `platformio.ini` blieb Arduinos `Serial` unverbunden — alle `Serial`-Ausgaben gingen ins Leere (nur ESP-IDF-Systemlogs kamen über den Monitor durch). Fix: beide Build-Flags ergänzt.
2. **Falsches WLAN-Passwort** in der ursprünglichen Funktionsbeschreibung → `4WAY_HANDSHAKE_TIMEOUT` (Reason 15). Durch Verbose-Logging (`esp_log_level_set` + `CORE_DEBUG_LEVEL=5`, temporär) diagnostiziert. Nutzer hat das Passwort korrigiert.
3. Reconnect-Logik darf `WiFi.begin()` nicht zu früh erneut aufrufen, während der Treiber noch verbindet, sonst `wifi:sta is connecting, return error`. Gelöst durch Statusprüfung vor jedem Reconnect-Versuch und Intervall von 15 s.

## Test / Ergebnis

- Board verbindet zuverlässig, `Logger`-Ausgabe: `[WiFi] Verbunden. IP: 192.168.10.33`.
- Verifiziert per PlatformIO „Upload and Monitor“ in VS Code (löst automatisch einen Reset beim Verbinden aus).

## Hinweis

Netzwerksegment ESP (`192.168.10.x`) vs. MQTT-Broker (`192.168.1.123`) beim Test war kein Problem: Broker im Heimnetz, Testgerät im Ferienhaus, beide Router per VPN verbunden (siehe `docs/requirements.md`, Entscheidungshistorie).
