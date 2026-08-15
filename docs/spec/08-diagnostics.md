# Phase 8 — Diagnostics

**Status:** ✅ Erledigt & getestet

## Ziel

I2C-Bus-Status und letzte Fehlermeldung über MQTT sichtbar machen.

## Voraussetzungen

- Phase 2 (MQTT-Grundgerüst) ✅

## Umsetzung

- Neue Klasse `Diagnostics` (`src/Diagnostics.h/.cpp`):
  - `i2cStatus` (`ok`/`error`) anhand `Wire.endTransmission()` auf den MCP23017 periodisch prüfen (via `I2CManager`) und bei Änderung publizieren.
  - `lastError` mit Echtzeit-Zeitstempel bei Fehlern (I2C-Ausfall, ungültige MQTT-Payloads, WLAN-Verlust, ...) retained setzen (NTP-Sync ist bereits in Phase 2 umgesetzt).
  - `Logger` könnte hier direkt als Quelle dienen (z. B. `ERROR`-Log-Einträge automatisch auch nach `diagnostics/lastError` spiegeln) — Kopplung zur Entscheidung.

## Betroffene Dateien

- `src/Diagnostics.h`, `src/Diagnostics.cpp` (neu)

## MQTT-Topics

- `gartenwasser/diagnostics/i2cStatus` (publish, bei Änderung)
- `gartenwasser/diagnostics/lastError` (publish, bei Fehler)

## Test

1. MCP23017-Verkabelung kurz trennen → `i2cStatus` wird `error`, `lastError` erhält passende Meldung.
2. Wieder verbinden → `i2cStatus` zurück auf `ok`.
3. Ungültigen `time/set`-Wert senden (siehe Phase 5) → `lastError` wird aktualisiert.

## Test / Ergebnis

- Auf Hardware getestet, vom Nutzer bestätigt.

## Umsetzung (finale Kopplung)

- Entscheidung zur „Kopplung" oben: `Logger` bekommt einen generischen `setErrorCallback()` (Funktionszeiger), den `Diagnostics::begin()` registriert. Jeder `Logger::Type::ERROR`-Aufruf ruft den Callback automatisch mit der Meldung auf — `Logger` kennt `Diagnostics` dabei nicht (lose Kopplung). Dadurch landen I2C-Ausfall, ungültige MQTT-Payloads und WLAN-Verlust automatisch in `lastError`, ohne bestehende Log-Aufrufe in `WifiManager`/`MqttManager` anzufassen.
- `Diagnostics::begin()` wird ganz am Anfang von `main.cpp`s `setup()` aufgerufen (vor `ConfigStore::begin()`), damit auch frühe Boot-Fehler (SPIFFS-Mount, WLAN-Timeout) erfasst werden.
- Der periodische `i2cStatus`-Check läuft wie der Ventil-Timer-Tick unabhängig von WLAN/MQTT-Verbindungsstatus.
