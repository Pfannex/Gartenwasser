# Phase 12 — Aufräumen/Refactoring

**Status:** 📋 Geplant (Teile bereits vorgezogen)

## Ziel

Code-Qualität und Wartbarkeit vor dem produktiven Einsatz sicherstellen.

## Bereits vorgezogen (vor Phase 2)

Ein Teil dieser Phase wurde bereits umgesetzt, um auf einer sauberen Basis mit Phase 2 zu starten:

- `Logger`-Klasse für einheitliches Log-Format (`src/Logger.h/.cpp`).
- `HmiManager` (Display/Touch/LVGL) aus `main.cpp` ausgelagert.
- `I2CManager` (I2C-Bus-Scan, MCP23017-Grundsetup) aus `main.cpp` ausgelagert.
- `WifiManager` nutzt jetzt `Logger` statt direktem `Serial`.
- `main.cpp` ist ein schlanker Orchestrator.

## Noch offen

- Broker-/Topic-Präfix zentral konfigurierbar machen (aktuell hart in `secrets.h`/Klassen verteilt).
- Code-Review über alle bis dahin entstandenen Klassen.
- `MqttManager`, `Diagnostics`, `Logger` koppeln: `ERROR`-Log-Einträge optional automatisch nach `diagnostics/lastError` spiegeln (siehe Phase 8).
- Kurze Test-Checkliste für zukünftige Firmware-Updates dokumentieren (manueller Regressionstest vor jedem Release).
- Speicher-/Performance-Check (RAM/Flash-Auslastung) nach vollständiger Implementierung.

## Test

- Vollständiger Regressionsdurchlauf aller MQTT-Topics laut `docs/requirements.md`.
- HA-Dashboard-Kontrolle: alle Entities korrekt gruppiert, Verfügbarkeit korrekt.
