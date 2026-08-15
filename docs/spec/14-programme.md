# Phase 14 — Bewässerungsprogramme (`main/program/cmd`, `main/program/state`)

**Status:** 📋 Backlog

## Ziel

Mehrere benannte Presets (`time` **und** `auto` je Ventil) im Gerät speichern und per einfachem Integer-Befehl auswählen — z. B. `SHORT`, `MEDIUM`, `LONG`, `TEST`. Maximale Flexibilität: ein Programm bestimmt nicht nur *wie lange*, sondern auch *welche* Ventile überhaupt teilnehmen (z. B. „nur Rasen, nicht die Beete“).

## Voraussetzungen

- Phase 7 (Automatik-Sequenz/Sequencer) — Programme steuern u. a. die `auto`-Zugehörigkeit, die der Sequencer liest.
- Phase 11 (`main/config/set`/`state`, JSON-Infrastruktur) — Programme sind strukturell eine Erweiterung desselben `config.json`.

## Umsetzung

- Programme als Array in `config.json`, erweiterbar über `main/config/set` (kein Firmware-Rebuild nötig für neue Programme):
  ```json
  "programs": [
    {"name": "SHORT",  "time": {"V1":2, "V2":2, "V3":2, "V4":2, "V5":2}, "auto": {"V1":true, ..., "V5":true}},
    {"name": "MEDIUM", "time": {...}, "auto": {...}},
    {"name": "LONG",   "time": {...}, "auto": {...}},
    {"name": "TEST",   "time": {"V1":1, ...}, "auto": {...}}
  ]
  ```
- `main/program/cmd <integer>`: wendet das Programm mit diesem Index **einmalig** an — überschreibt die aktuellen `time`/`auto`-Werte je Ventil (persistiert via `ConfigStore`, publiziert die betroffenen `V{n}/time/state`/`auto/state`). Kein dauerhaftes Lock: einzelne Ventile bleiben danach normal über `V{n}/time/set`/`auto/set` änderbar, bis das nächste Mal ein Programm gewählt wird.
- `main/program/state` (retained): aktiver Index, plus Name (z. B. Payload `2 (LONG)` oder als JSON `{"index":2,"name":"LONG"}` — Format bei Umsetzung final festlegen).
- Ungültiger Index (außerhalb des Arrays) → ignorieren + loggen (`ERROR`), wie bei allen anderen Validierungen.
- `maxTime` bleibt von der Programmwahl unberührt (globale Obergrenze, siehe Phase 5) — ein Programm kann sie nicht aushebeln.

## Betroffene Dateien

- `src/MqttManager.h/.cpp` (Subscribe-Handler `main/program/cmd`, Publish `main/program/state`)
- `src/ConfigStore.h/.cpp` (Programme-Array laden/speichern/anwenden)

## MQTT-Topics

- `gartenwasser/main/program/cmd` (subscribe, `<integer>`, nicht retained)
- `gartenwasser/main/program/state` (publish, retained)

## Test

1. `mosquitto_pub -t gartenwasser/main/program/cmd -m 3` (TEST) → alle `V{n}/time/state` und `auto/state` ändern sich passend zum TEST-Programm, `main/program/state` zeigt `3`.
2. Einzelnes Ventil danach manuell per `V2/time/set` ändern → funktioniert normal, Programm-Auswahl bleibt bei `3` (kein Lock).
3. Ungültigen Index (z. B. `99`) senden → wird ignoriert, `main/program/state` unverändert, Log-Eintrag `ERROR`.
4. Neustart des Boards → zuletzt gewähltes Programm (Index) bleibt als reiner Konfigurationswert erhalten, es wird aber **nichts** automatisch gestartet (sicherer Boot-Grundzustand, siehe `docs/requirements.md`).
