# Phase 11 — Konfiguration per JSON (`main/config/set`, `main/config/state`)

**Status:** ✅ Erledigt & getestet

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

## Vollständiges Beispiel (alle Elemente)

Genau die Struktur, die auch `main/config/state` retained liefert bzw. die man komplett auf `main/config/set` zurückspielen kann (z. B. als gespeichertes Backup/Preset). `alias` enthält zusätzlich `V0` (Hauptventil, siehe Phase 9); `time`/`auto` gibt es nur für `V1`–`V5`.

```json
{
  "time": {"V1": 5, "V2": 10, "V3": 5, "V4": 15, "V5": 5},
  "auto": {"V1": true, "V2": true, "V3": false, "V4": true, "V5": false},
  "alias": {
    "V0": "Hauptventil",
    "V1": "Rasen Vorgarten",
    "V2": "Rasen Garten",
    "V3": "Beet Rosen",
    "V4": "Beet Gemüse",
    "V5": "Kübelpflanzen"
  },
  "maxTime": 30
}
```

- Alle Keys sind optional — dieses Beispiel zeigt den Vollzustand, ein `set` kann jede beliebige Teilmenge davon enthalten (siehe „Umsetzung" oben).
- `main/config/state` liefert exakt diese Struktur (kompakt, ohne Einrückung) als ein JSON-Objekt.

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

## Test / Ergebnis

- Alle vier Testfälle auf Hardware verifiziert (Teil-Update, mehrere Domänen, unbekannter Key ignoriert, `config/state` sofort verfügbar).

## Umsetzung (Details während der Implementierung)

- `ConfigStore` bekommt `toJson()`, das dieselbe `buildJson()`-Struktur nutzt wie `save()` (kein doppelt gepflegtes Schema).
- Statt eigener Validierungslogik im JSON-Handler wurden die bestehenden `V{n}/time/set`/`auto/set`/`alias/set`-Handler in eine reine `apply*Value()`-Kernfunktion (Validierung + Persistierung + Publish, ohne Payload-Parsing) und einen dünnen `handle*()`-Wrapper (parst den MQTT-String-Payload) aufgeteilt. `handleConfigSet()` ruft dieselben `apply*Value()`-Funktionen direkt mit den JSON-Werten auf — keine doppelte Validierung.
- `maxTime` ist damit zum ersten Mal setzbar — ausschließlich über `main/config/set`, wie geplant kein eigenes `main/time/set`.
- PubSubClient-Puffer (Default 256 Byte) reicht nicht für die volle Konfiguration inkl. aller Aliase als JSON → `setBufferSize(1024)` in `MqttManager::begin()`.
- `main/config/state` wird nach jeder Änderung publiziert, unabhängig davon, über welches Topic sie kam (auch die einzelnen `V{n}/...`-Topics lösen es aus).
