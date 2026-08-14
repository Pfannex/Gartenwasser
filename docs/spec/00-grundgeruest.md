# Phase 0 — Grundgerüst

**Status:** ✅ Erledigt & getestet

## Ziel

Projekt-Grundgerüst für WLAN/MQTT vorbereiten, ohne die bestehende Display/Touch/I2C-Basis zu verändern.

## Umsetzung

- `platformio.ini`: `lib_deps` ergänzt um `knolleary/PubSubClient` und `bblanchon/ArduinoJson`.
- `include/secrets.h` (nicht versioniert, siehe `.gitignore`) mit WLAN- und MQTT-Zugangsdaten angelegt.
- `include/secrets.h.example` als Vorlage für neue Entwicklungsumgebungen.

## Betroffene Dateien

- `platformio.ini`
- `include/secrets.h`, `include/secrets.h.example`
- `.gitignore`

## Test / Ergebnis

- `pio run` kompiliert erfolgreich mit den neuen Abhängigkeiten (Flash 16,7 %, RAM 24,4 % zu diesem Zeitpunkt).
