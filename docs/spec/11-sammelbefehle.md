# Phase 11 — Konfiguration per JSON (`main/config/set`, `main/config/state`)

**Status:** 📋 Geplant

## Ziel

Komplette oder teilweise Konfiguration (`time`, `auto`, `maxTime`, später `alias`) in einem MQTT-Befehl setzen bzw. als Snapshot lesen — z. B. aus einer HA-Automatisierung heraus oder für Backup/Restore und wiederverwendbare Presets ("Default"-/"Test"-Konfiguration als extern gespeicherte JSON-Payloads, ohne Firmware-seitige Presets).

Ursprünglich als domänenspezifische Sammel-Befehle (`main/time/set`, `main/auto/set`) geplant — stattdessen ein einziges, vollständiges `config`-JSON, das dieselbe Funktion abdeckt und zusätzlich einen lesbaren Gesamt-Snapshot liefert (siehe `docs/requirements.md`, Entscheidungshistorie 2026-08-15).

## Voraussetzungen

- Phase 5 (Laufzeit, `ConfigStore`) ✅
- Phase 6 (Automatik-Flag) ✅

## Umsetzung

- `main/config/set`: JSON parsen (via `ArduinoJson`), Struktur identisch zur internen `ConfigStore`-Persistenz:
  ```json
  {"time":{"V1":10,"V2":5},"auto":{"V3":true},"maxTime":30}
  ```
  Alle Keys optional (Teil-Update) — nicht enthaltene Ventile/Werte bleiben unverändert (**kein** Rücksprung auf Defaults, anders als beim Laden von `/config.json` nach einem Boot ohne vorhandene Datei).
- Jeder enthaltene Wert durchläuft dieselbe Validierung wie die einzelnen `V{n}/time/set`/`V{n}/auto/set`-Handler (ungültige Werte ignorieren + loggen statt übernehmen, gültige Werte übernehmen). Unbekannte Keys (z. B. `"V9"`) werden ignoriert.
- Naheliegend: `ConfigStore` um eine JSON-Serialisierung/-Deserialisierung erweitern, die sowohl von `save()`/`load()` (SPIFFS) als auch vom MQTT-Handler genutzt wird, statt die Struktur doppelt zu pflegen.
- `main/config/state`: aktuelle Gesamt-Konfiguration (alle Ventile, `maxTime`) als JSON, retained. Wird bei jeder Änderung (egal ob über `config/set` oder die einzelnen `V{n}/...`-Topics) sowie nach jedem (Re-)Connect neu publiziert.
- Rohes JSON passt nicht in ein einzelnes HA-Discovery-Entity — beide Topics sind vorerst nur für direkte MQTT-Nutzung/Automatisierungen/Backup gedacht (kein eigenes HA-Entity in Phase 10 vorgesehen).

## Betroffene Dateien

- `src/MqttManager.h/.cpp` (Subscribe-Handler für `main/config/set`, Publish für `main/config/state`)
- `src/ConfigStore.h/.cpp` (ggf. JSON-Serialisierung/-Deserialisierung für Wiederverwendung)

## MQTT-Topics

- `gartenwasser/main/config/set` (subscribe, JSON, nicht retained)
- `gartenwasser/main/config/state` (publish, JSON, retained)

## Test

1. `mosquitto_pub -t gartenwasser/main/config/set -m '{"maxTime":30,"time":{"V1":8,"V2":12}}'` → `main/time/maxTime`, `V1/time/state` und `V2/time/state` werden aktualisiert, `main/config/state` published den neuen Gesamtstand.
2. `mosquitto_pub -t gartenwasser/main/config/set -m '{"auto":{"V1":true,"V3":false}}'` → `V1/auto/state` und `V3/auto/state` werden aktualisiert, andere Werte (`time`, `maxTime`) bleiben unverändert.
3. Unbekannten Key (`{"time":{"V9":5}}`) senden → wird ignoriert, kein Fehler/Crash.
4. `mosquitto_sub -t gartenwasser/main/config/state -v` → liefert sofort (retained) die komplette aktuelle Konfiguration, auch ohne vorherigen `set`.
