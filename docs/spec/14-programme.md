# Phase 14 — Bewässerungsprogramme (`main/program/cmd`, `main/program/state`)

**Status:** 📋 Geplant (Design abgestimmt, Umsetzung offen)

## Ziel

Mehrere benannte Presets (`time` **und** `auto` je Ventil) im Gerät speichern und per einfachem Integer-Befehl auswählen. Maximale Flexibilität: ein Programm bestimmt nicht nur *wie lange*, sondern auch *welche* Ventile überhaupt teilnehmen (z. B. „nur Rasen, nicht die Beete“). Die Touch-UI-Buttons `P1`–`P4` (Phase 13, bereits vorhanden, aktuell ohne Funktion) werden an die ersten vier Programme angebunden — dieser Teil folgt in einem eigenen Schritt nach der Config selbst.

## Voraussetzungen

- Phase 7 (Automatik-Sequenz/Sequencer) ✅ — Programme steuern die `auto`-Zugehörigkeit, die der Sequencer liest.
- Phase 11 (`main/config/set`/`state`, JSON-Infrastruktur) ✅ — Programme sind strukturell eine Erweiterung derselben `config.json`, kein neues System.
- Phase 13 (Touch-UI) ✅ — `P1`–`P4`-Platzhalter-Buttons warten bereits auf die Anbindung.

## Design (abgestimmt, 2026-08-16)

### JSON-Schema

Erweitert dieselbe Struktur, die `ConfigStore`/`main/config/set`/`main/config/state` bereits nutzen (siehe Phase 11), um zwei weitere Top-Level-Keys:

```json
{
  "time": {"V1": 5, "V2": 10, "V3": 5, "V4": 15, "V5": 5},
  "auto": {"V1": true, "V2": true, "V3": false, "V4": true, "V5": false},
  "alias": {"V0": "Hauptventil", "V1": "Rasen Vorgarten", "V2": "Rasen Garten", "V3": "Beet Rosen", "V4": "Beet Gemüse", "V5": "Kübelpflanzen"},
  "maxTime": 30,
  "programs": [
    {"name": "Kurz",  "time": {"V1": 2, "V2": 2}, "auto": {"V1": true, "V2": true, "V3": false, "V4": false, "V5": false}},
    {"name": "Rasen", "time": {"V1": 10, "V2": 10}, "auto": {"V1": true, "V2": true, "V3": false, "V4": false, "V5": false}},
    {"name": "Alles", "time": {"V1": 8, "V2": 8, "V3": 12, "V4": 15, "V5": 6}, "auto": {"V1": true, "V2": true, "V3": true, "V4": true, "V5": true}},
    {"name": "Test",  "time": {"V1": 1, "V2": 1, "V3": 1, "V4": 1, "V5": 1}, "auto": {"V1": true, "V2": true, "V3": true, "V4": true, "V5": true}}
  ],
  "activeProgram": 2
}
```

Vollständiges Beispiel mit allen Elementen auch in `docs/requirements.md`.

### Kernentscheidungen

1. **1-basierte Nummerierung**, passend zu den Touch-UI-Buttons `P1`–`P4` und dem bestehenden `V1`–`V5`-Schema. `0` = „kein Programm gewählt“ — Startwert nach Boot **und** explizit setzbar (`main/program/cmd 0` löscht die Auswahl, ohne Ventile anzufassen).
2. **`time`/`auto` je Programm sind Teilmengen** — exakt dieselbe Semantik wie bei `main/config/set` (Phase 11): enthaltene Felder werden beim Anwenden übernommen, fehlende bleiben unverändert. Keine Sonderregel „auto muss vollständig sein“ — ein Regelwerk für alles ist einfacher zu merken und zu warten. Konsequenz: „nur Rasen, nicht die Beete“ muss ein Programm **explizit** mit `"auto":{"V4":false,"V5":false}` festhalten, sonst bleibt der vorherige Zustand der Beet-Ventile stehen.
3. **Anwenden = Wiederverwendung von Phase 11**: `main/program/cmd <n>` ruft für jedes im Programm enthaltene Feld dieselben `applyTimeValue()`/`applyAutoValue()`-Kernfunktionen auf, die `main/config/set` bereits nutzt — keine neue Validierungslogik.
4. **`maxTime` und `alias` sind kein Teil eines Programms** — `maxTime` bleibt die geräteweite Sicherheitsgrenze (Phase 5), `alias` ist Ventil-Identität, hat mit Bewässerungsdauer nichts zu tun.
5. **Editieren der Programme läuft über das bestehende `main/config/set`** — der Key `"programs"` ist dabei „ganzes Array ersetzen, wenn mitgeschickt“ (kein Merge einzelner Programme, da Arrays keine natürlichen Schlüssel haben). Praktischer Ablauf: `main/config/state` holen, lokal bearbeiten, komplett zurückschicken.
6. **Obergrenze `ConfigStore::kMaxPrograms = 8`** — mehr als die 4 physischen Touch-Buttons, damit per MQTT/später Phase 15 (Zeitplan) noch Luft ist, aber als feste Array-Größe (passt zum bisherigen Stil mit festen C-Arrays statt dynamischer Allokation). Programmname wiederverwendet `ConfigStore::kAliasMaxLength` (32 Zeichen).
7. **Persistenz**: `programs` + `activeProgram` beides in `/config.json`. Nach Neustart bleibt die letzte Auswahl als reiner Info-Wert erhalten, startet aber nichts automatisch (sicherer Grundzustand, wie überall sonst auch).

### Technische Konsequenz

`ConfigStore::kJsonCapacity` (aktuell 768 Byte) reicht mit bis zu 8 Programmen nicht mehr — muss auf ca. **2048 Byte** angehoben werden, inkl. passender Anhebung des MQTT-Puffers in `MqttManager` (`setBufferSize()`, aktuell 1024).

## Umsetzung (im Detail bei der Implementierung)

- `ConfigStore`: `programs`-Array + `activeProgram` laden/speichern (Erweiterung von `buildJson()`), neue Getter (`getProgramCount()`, `getProgramName(index)`, `getProgramValveTime(index, valve)`, `getProgramValveAuto(index, valve)`, `getActiveProgram()`/`setActiveProgram()`).
- `MqttManager`: `main/program/cmd`-Handler wendet das Programm an (Schleife über V1–V5, `applyTimeValue()`/`applyAutoValue()` wiederverwenden), publiziert `main/program/state` und `main/config/state` (Programmwahl ist Teil der Gesamt-Konfiguration).
- Anbindung der Touch-UI-Buttons `P1`–`P4` (Phase 13) an die ersten vier Programme folgt **danach**, als eigener Schritt.

## Betroffene Dateien

- `src/ConfigStore.h/.cpp` (Programme-Array + `activeProgram`)
- `src/MqttManager.h/.cpp` (Subscribe-Handler `main/program/cmd`, Publish `main/program/state`)
- `src/HmiManager.h/.cpp` (später: `P1`–`P4`-Anbindung)

## MQTT-Topics

- `gartenwasser/main/program/cmd` (subscribe, `<integer>`, 1-basiert, `0` = keine Auswahl, nicht retained)
- `gartenwasser/main/program/state` (publish, retained, JSON `{"index":1,"name":"Kurz"}`, bei `0` `{"index":0,"name":null}`)

## Test

1. `mosquitto_pub -t gartenwasser/main/program/cmd -m 4` (Test) → alle im Programm enthaltenen `V{n}/time/state`/`auto/state` ändern sich, `main/program/state` zeigt `{"index":4,"name":"Test"}`.
2. Einzelnes Ventil danach manuell per `V2/time/set` ändern → funktioniert normal, Programm-Auswahl bleibt bei `4` (kein Lock).
3. Ungültigen Index (z. B. `99`) senden → wird ignoriert, `main/program/state` unverändert, Log-Eintrag `ERROR`.
4. `main/program/cmd 0` → `main/program/state` zeigt `{"index":0,"name":null}`, keine Ventile werden angefasst.
5. Neustart des Boards → zuletzt gewähltes Programm (Index) bleibt als reiner Konfigurationswert erhalten, es wird aber **nichts** automatisch gestartet (sicherer Boot-Grundzustand, siehe `docs/requirements.md`).
6. `main/config/set` mit `"programs":[...]` → komplettes Array wird ersetzt, `main/config/state` zeigt den neuen Stand.
