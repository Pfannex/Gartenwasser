# Phase 14 — Bewässerungsprogramme (`main/program/cmd`, `main/program/state`)

**Status:** ✅ Erledigt & getestet

## Ziel

Mehrere benannte Presets (`time` **und** `auto` je Ventil) im Gerät speichern und per einfachem Integer-Befehl auswählen. Maximale Flexibilität: ein Programm bestimmt nicht nur *wie lange*, sondern auch *welche* Ventile überhaupt teilnehmen (z. B. „nur Rasen, nicht die Beete“). Die Touch-UI-Buttons `P1`–`P4` (Phase 13) sind über das `shortcut`-Feld an frei wählbare Programme gebunden (nicht zwingend die ersten vier).

## Voraussetzungen

- Phase 7 (Automatik-Sequenz/Sequencer) ✅ — Programme steuern die `auto`-Zugehörigkeit, die der Sequencer liest.
- Phase 11 (`main/config/set`/`state`, JSON-Infrastruktur) ✅ — Programme sind strukturell eine Erweiterung derselben `config.json`, kein neues System.
- Phase 13 (Touch-UI) ✅ — `P1`–`P4`-Platzhalter-Buttons warten bereits auf die Anbindung.

## Design (abgestimmt, 2026-08-16, Persistenz/Topics am 2026-08-16 auf eigenen Bereich umgestellt)

Programme leben in einem eigenen Bereich, getrennt von `config` (siehe `docs/requirements.md`, Abschnitt „Konfiguration“) — eigene Datei `/programs.json`, eigenes Topic-Paar `main/programs/set`/`main/programs/state` für Bulk-Editieren, plus das schlanke `main/program/cmd`/`main/program/state` (Singular) zur Auswahl per Index.

### JSON-Schema

`main/programs/state` (und `main/programs/set` für Bulk-Updates):

```json
{
  "programs": [
    {"name": "Kurz",  "shortcut": "P1", "time": {"V1": 2, "V2": 2}, "auto": {"V1": true, "V2": true, "V3": false, "V4": false, "V5": false}},
    {"name": "Rasen", "shortcut": "P2", "time": {"V1": 10, "V2": 10}, "auto": {"V1": true, "V2": true, "V3": false, "V4": false, "V5": false}},
    {"name": "Alles", "shortcut": "P3", "time": {"V1": 8, "V2": 8, "V3": 12, "V4": 15, "V5": 6}, "auto": {"V1": true, "V2": true, "V3": true, "V4": true, "V5": true}},
    {"name": "Test",  "time": {"V1": 1, "V2": 1, "V3": 1, "V4": 1, "V5": 1}, "auto": {"V1": true, "V2": true, "V3": true, "V4": true, "V5": true}}
  ],
  "activeProgram": 2
}
```

`shortcut` (optional, 2026-08-16 ergänzt): `"P1"`–`"P4"`, bindet ein Programm an einen der vier physischen Touch-UI-Buttons (Phase 13), unabhängig von seiner Position im Array. Fehlt der Key, ist das Programm nur per `main/program/cmd <index>`/`main/programs/set` erreichbar, nicht über einen Button (wie „Test“ im Beispiel oben).

`main/program/state` (Singular, kompakter Ausschnitt derselben Information):

```json
{"index": 2, "name": "Rasen"}
```

Vollständiges Beispiel mit allen Elementen auch in `docs/requirements.md`.

### Kernentscheidungen

1. **1-basierte Nummerierung**, passend zu den Touch-UI-Buttons `P1`–`P4` und dem bestehenden `V1`–`V5`-Schema. `0` = „kein Programm gewählt“ — Startwert nach Boot **und** explizit setzbar (`main/program/cmd 0` löscht die Auswahl, ohne Ventile anzufassen).
2. **`time`/`auto` je Programm sind Teilmengen** — exakt dieselbe Semantik wie bei `main/config/set` (Phase 11): enthaltene Felder werden beim Anwenden übernommen, fehlende bleiben unverändert. Keine Sonderregel „auto muss vollständig sein“ — ein Regelwerk für alles ist einfacher zu merken und zu warten. Konsequenz: „nur Rasen, nicht die Beete“ muss ein Programm **explizit** mit `"auto":{"V4":false,"V5":false}` festhalten, sonst bleibt der vorherige Zustand der Beet-Ventile stehen.
3. **Anwenden = Wiederverwendung von Phase 11**: sowohl `main/program/cmd <n>` als auch `activeProgram` in `main/programs/set` rufen für jedes im Programm enthaltene Feld dieselben `applyTimeValue()`/`applyAutoValue()`-Kernfunktionen auf, die `main/config/set` bereits nutzt — keine neue Validierungslogik.
4. **`maxTime` und `alias` sind kein Teil eines Programms** — `maxTime` bleibt die geräteweite Sicherheitsgrenze (Phase 5), `alias` ist Ventil-Identität, hat mit Bewässerungsdauer nichts zu tun.
5. **Bulk-Editieren über das eigene `main/programs/set`** (nicht über `main/config/set`, siehe Entscheidungshistorie 2026-08-16 in `docs/requirements.md`) — der Key `"programs"` ist dabei „ganzes Array ersetzen, wenn mitgeschickt“ (kein Merge einzelner Programme, da Arrays keine natürlichen Schlüssel haben), der Key `"activeProgram"` wendet die Auswahl an (identisch zu `main/program/cmd`). Praktischer Ablauf: `main/programs/state` holen, lokal bearbeiten, komplett zurückschicken.
6. **Obergrenze `ConfigStore::kMaxPrograms = 8`** — mehr als die 4 physischen Touch-Buttons, damit per MQTT/später Phase 15 (Zeitplan) noch Luft ist, aber als feste Array-Größe (passt zum bisherigen Stil mit festen C-Arrays statt dynamischer Allokation). Programmname wiederverwendet `ConfigStore::kAliasMaxLength` (32 Zeichen).
7. **Persistenz**: `programs` + `activeProgram` beides in `/programs.json` (eigene Datei, siehe oben — `activeProgram` gehört inhaltlich zu den Programmen, nicht zu den Ventilparametern in `config.json`). Nach Neustart bleibt die letzte Auswahl als reiner Info-Wert erhalten, startet aber nichts automatisch (sicherer Grundzustand, wie überall sonst auch).
8. **`shortcut`-Feld für die Touch-UI-Bindung** (2026-08-16 ergänzt, für die anstehende `P1`–`P4`-Anbindung): optionales String-Feld je Programm, Werte `"P1"`–`"P4"`. Ungültige Werte (falscher String, falsche Groß-/Kleinschreibung) werden wie bei `alias`/`time`/`auto` ignoriert + geloggt, nicht die ganze `main/programs/set`-Anfrage abgelehnt. **Doppelt vergebene Shortcuts werden nicht beim Schreiben verhindert** — würde dem Array-Replace-Prinzip widersprechen (ein doppelter Shortcut irgendwo im Array müsste sonst den kompletten Satz zurückweisen). Stattdessen: beim Auflösen eines Buttons gewinnt der **erste Treffer in Array-Reihenfolge**, zusätzlich wird bei `main/programs/set` ein nicht-blockierender `ERROR`-Log-Eintrag geschrieben, falls ein Duplikat erkannt wird (Sichtbarkeit ohne Ablehnung). Intern in `ConfigStore` als einfacher `uint8_t` (0 = kein Shortcut, 1–4 = P1–P4) gespeichert, analog zu `activeProgram` — kein String-Vergleich zur Laufzeit nötig.

### Technische Konsequenz

Eigene Datei/eigenes Topic bedeutet: `ConfigStore::kJsonCapacity` für `config.json` bleibt unverändert bei 768 Byte (kein Wachstum durch Programme). Neue eigene Konstante `ConfigStore::kProgramsJsonCapacity` (ca. **2048 Byte** für bis zu 8 Programme) für `programs.json`/`main/programs/*`. Der MQTT-Puffer in `MqttManager` (`setBufferSize()`, aktuell 1024) muss auf die größte einzelne Payload angehoben werden — das bleibt `programs` mit ca. 2048 Byte.

## Umsetzung (im Detail bei der Implementierung)

- `ConfigStore`: neue Datei `/programs.json` (eigenes Load/Save/`toJson()`, analog zu `config.json`), `programs`-Array + `activeProgram`, neue Getter (`getProgramCount()`, `getProgramName(index)`, `getProgramValveTime(index, valve)`, `getProgramValveAuto(index, valve)`, `getActiveProgram()`/`setActiveProgram()`).
- `MqttManager`: `main/program/cmd`-Handler wendet das Programm an (Schleife über V1–V5, `applyTimeValue()`/`applyAutoValue()` wiederverwenden), publiziert `main/program/state` und `main/programs/state`. Zusätzlich `main/programs/set`-Handler (Bulk-JSON, Array-Replace + optional `activeProgram`) mit zugehörigem `main/programs/state`-Publish.

### `P1`–`P4`-Anbindung umgesetzt (2026-08-16)

- `ConfigStore`: `ProgramInput`/internes `StoredProgram` um `shortcut`-Feld (`uint8_t`, 0/1–4) erweitert, `buildProgramsJson()`/Laden lesen/schreiben den `"shortcut"`-Key (String `"P1"`–`"P4"` ↔ `1`–`4`, ungültig ↔ `0`/ignoriert, statische String-Literale statt lokaler Puffer für die JSON-Ausgabe, um Dangling-Pointer beim Serialisieren zu vermeiden). Neue Funktion `getProgramIndexForShortcut(uint8_t shortcut)` — linearer Scan über `getProgramCount()` Einträge, liefert den 1-basierten Programm-Index des **ersten** Treffers oder `0`. Duplikat-Erkennung direkt in `setPrograms()` (dort wird ohnehin einmalig über das komplette neue Array iteriert), Log-Meldung bewusst kurz gehalten (`"Shortcut Px doppelt belegt!"`) statt der ursprünglichen langen Variante, die im 96-Byte-`lastError`-Puffer abgeschnitten wurde.
- `MqttManager`: `handleProgramsSet()` liest `"shortcut"` je Programm mit ein. Neu: `requestProgramByShortcut(shortcut)` (öffentlich, für Touch) und `requestProgramClear()` (öffentlich, entspricht `main/program/cmd 0`).
- `HmiManager`: `P1`–`P4`-Button-Handler ruft `ConfigStore::getProgramIndexForShortcut()`; ist das gebundene Programm bereits aktiv, wählt ein erneuter Druck ab (`requestProgramClear()`, Toggle-Verhalten), sonst wird es angewendet (`requestProgramByShortcut()`). Ohne Bindung: transienter Hinweis „P{n} nicht konfiguriert!“. Checked-Status der Buttons wird periodisch aus `ConfigStore::getActiveProgram()` abgeleitet, nicht aus dem lokalen Klick-Zustand — bleibt so auch bei MQTT-Änderungen korrekt. Details siehe `docs/spec/13-touch-ui.md`.
- **Zusätzlich, im selben Zug entschieden** (nicht ursprünglich Teil des Designs, ergab sich aus dem Hardware-Test): `main/cmd ON` erfordert jetzt ein gewähltes Programm (`activeProgram != 0`), sowohl per Touch als auch per MQTT — siehe `docs/spec/07-automatik-sequenz.md`, Nachtrag, und Entscheidungshistorie in `docs/requirements.md`. Direktes Ventilschalten (`V{n}/cmd`) bleibt davon unberührt.

## Betroffene Dateien

- `src/ConfigStore.h/.cpp` (`/programs.json`, Programme-Array + `activeProgram` + `shortcut`, `getProgramIndexForShortcut()`)
- `src/MqttManager.h/.cpp` (Subscribe-Handler `main/program/cmd`, `main/programs/set`, Publish `main/program/state`, `main/programs/state`, `requestProgramByShortcut()`, `requestProgramClear()`, Programm-Pflicht in `startSequence()`)
- `src/HmiManager.h/.cpp` (`P1`–`P4`-Anbindung, Statuszeile/Hinweise, siehe `docs/spec/13-touch-ui.md`)

## MQTT-Topics

- `gartenwasser/main/program/cmd` (subscribe, `<integer>`, 1-basiert, `0` = keine Auswahl, nicht retained)
- `gartenwasser/main/program/state` (publish, retained, JSON `{"index":1,"name":"Kurz"}`, bei `0` `{"index":0,"name":null}`)
- `gartenwasser/main/programs/set` (subscribe, JSON, `"programs"` ersetzt das komplette Array, `"activeProgram"` wendet die Auswahl an, beide optional, nicht retained)
- `gartenwasser/main/programs/state` (publish, retained, JSON, kompletter Gesamtstand)

## Test

1. `mosquitto_pub -t gartenwasser/main/program/cmd -m 4` (Test) → alle im Programm enthaltenen `V{n}/time/state`/`auto/state` ändern sich, `main/program/state` zeigt `{"index":4,"name":"Test"}`, `main/programs/state` zeigt `"activeProgram":4`.
2. Einzelnes Ventil danach manuell per `V2/time/set` ändern → funktioniert normal, Programm-Auswahl bleibt bei `4` (kein Lock).
3. Ungültigen Index (z. B. `99`) senden → wird ignoriert, `main/program/state` unverändert, Log-Eintrag `ERROR`.
4. `main/program/cmd 0` → `main/program/state` zeigt `{"index":0,"name":null}`, keine Ventile werden angefasst.
5. Neustart des Boards → zuletzt gewähltes Programm (Index) bleibt als reiner Konfigurationswert erhalten, es wird aber **nichts** automatisch gestartet (sicherer Boot-Grundzustand, siehe `docs/requirements.md`).
6. `main/programs/set` mit `{"programs":[...]}` → komplettes Array wird ersetzt, `main/programs/state` zeigt den neuen Stand, `main/config/state` bleibt davon unberührt.
7. `P1`-`P4` am Touch-Display drücken → passendes Programm wird angewendet (Statuszeile zeigt Programmname), erneuter Druck auf den bereits aktiven Button wählt wieder ab (Statuszeile zurück auf „MANUELL“).
8. `P3`/`P4` ohne `shortcut`-Bindung drücken → transienter Hinweis „P{n} nicht konfiguriert!“, keine Änderung an Ventilen/Auswahl.
9. 8 Programme laden, eines ohne `shortcut` aktivieren → keiner der `P1`-`P4`-Buttons leuchtet, Statuszeile zeigt trotzdem den Programmnamen.
10. `main/cmd ON` (Touch **und** MQTT) ohne gewähltes Programm → bleibt wirkungslos (`main/state` bleibt `OFF`), Log-Eintrag `"main/cmd ON ignoriert: kein Programm gewaehlt."`; mit gewähltem Programm startet es normal.

## Test / Ergebnis

Strukturierte Ergebnistabelle (Prüfpunkt/Test/Ergebnis/Bewertung) siehe **`docs/testing.md`**, Abschnitt „Phase 14 — Bewässerungsprogramme": 14 von 14 Checks bestanden (automatisiert per Python/paho-mqtt gegen den echten Broker, alle 6 Testfälle oben plus ein Bonus-Test für die Teilmengen-Semantik).

**Bug gefunden und gefixt**: `main/programs/set` liess das Board mit einem Stack-Overflow abstuerzen (`Guru Meditation Error: Core 0 panic'ed (Stack protection fault)`, Serial-Monitor-Mitschnitt bestaetigt `Detected in task "loopTask"`). Ursache: mehrere grosse JSON-Puffer (`StaticJsonDocument<2048>`, `char payloadStr[2048]`) gleichzeitig auf dem Stack der Arduino-`loopTask`, deren Default-Groesse (8192 Byte) dafuer nicht mehr reichte. Fix: `SET_LOOP_TASK_STACK_SIZE(16 * 1024)` in `main.cpp` (RAM-Headroom war reichlich vorhanden, siehe Phase-12-Speicher-Check).

**`P1`–`P4`-Anbindung + Programm-Pflicht (2026-08-16)**: interaktiv auf Hardware getestet (Nutzer bedient Display, Ergebnis per gezielten MQTT-Abfragen gegengeprüft) — alle Testfälle 7–10 oben bestanden. Zweiter, kleinerer Bug unterwegs gefunden und gefixt: die Duplikat-Log-Meldung für Shortcuts wurde im 96-Byte-`lastError`-Puffer abgeschnitten, auf `"Shortcut Px doppelt belegt!"` gekürzt.

## Nachtrag (2026-08-17): `P1`–`P4`/`shortcut` wieder entfernt

Im Zuge der Touch-UI-Neugestaltung (siehe `docs/spec/13-touch-ui.md`, Nachtrag) wurden die festen `P1`–`P4`-Buttons durch eine „Programme“-Unterseite ersetzt, die per `<`/`>` durch **alle** Programme blättert statt nur die vier gebundenen. Damit war das oben beschriebene `shortcut`-Feld (samt `getProgramIndexForShortcut()`, `requestProgramByShortcut()`, `requestProgramClear()`) ohne Zweck und wurde komplett entfernt, nicht nur ungenutzt gelassen — Details siehe `docs/spec/13-touch-ui.md`. `kMaxPrograms` gleichzeitig von 8 auf 32 angehoben. Die Grundsatzentscheidung „Automatik erfordert Programm“ (Test 10 oben) bleibt unverändert bestehen.

## Nachtrag (2026-08-18): manuelle `time`/`auto`-Änderung setzt Programmwahl zurück ("MANUELL")

Ausgangspunkt war eine Beobachtung des Nutzers beim Testen von Phase 18 (Web-Konfigurationsseite): wählt man z. B. Programm „Kurz“ (nur `V1` auf `auto`) und schaltet danach manuell zusätzlich `V2` auf `auto`, zeigte die Oberfläche weiterhin „Programm: Kurz“, obwohl `V2` jetzt ebenfalls mitliefe — fühlte sich inkonsistent an. Bei der Analyse zeigte sich: das Problem ist noch grundlegender, als es aussah. `startSequence()` ruft vor jedem Start ohnehin `applyProgram(activeProgram)` erneut auf, extra um „Drift durch zwischenzeitliche manuelle Änderungen“ zu verhindern (siehe Kernentscheidung 3 oben) — die manuelle `V2`-Änderung hätte also beim nächsten Start ohnehin **stillschweigend wieder verworfen** werden, ohne dass die Oberfläche das je angezeigt hätte.

**Entscheidung**: jede direkte `time`/`auto`-Änderung außerhalb einer Programm-Anwendung (`V{n}/time/set`, `V{n}/auto/set`, `main/config/set` mit `time`/`auto`-Keys) setzt `activeProgram` jetzt sofort auf `0` zurück — technisch über einen erneuten Aufruf von `applyProgram(0)` (dieselbe Funktion, die schon das Auto-Reset für „Kein Programm“ übernimmt, siehe Nachtrag 2026-08-16 oben). Neue gemeinsame Hilfsfunktion `MqttManager::publishConfigStateAndClearProgram()`, aufgerufen von `handleTimeSet()`, `handleAutoSet()` und (nur wenn `time`/`auto`-Keys tatsächlich vorkamen) `handleConfigSet()` — `maxTime`/`alias` lösen bewusst **keine** Rücksetzung aus, da sie kein Teil eines Programms sind (Kernentscheidung 4). Das Anwenden eines Programms selbst (`applyProgram()`) ruft `applyTimeValue()`/`applyAutoValue()` weiterhin direkt auf und bleibt von der neuen Rücksetzung unberührt (kein Selbst-Abbruch).

Konsequenz, vom Nutzer explizit gewünscht: „auto“ ist damit **ausschließlich** über Programme setzbar — die Auto-Toggle-Buttons wurden aus der Web-Konfigurationsseite entfernt (siehe `docs/spec/18-webif-konfiguration.md`, Nachtrag). `time`/`alias` bleiben dort editierbar (steuern u. a. die Dauer eines manuellen Einzelventil-Starts, unabhängig von jedem Programm), lösen bei Änderung aber „MANUELL“ aus. Die UI-seitige Beschriftung dieses Zustands (`activeProgram == 0`) ist überall dieselbe wie bisher: Touch-UI-Statuszeile „MANUELL“ (unverändert), Touch-UI-Programme-Button jetzt „Manueller Modus“ (siehe `docs/spec/13-touch-ui.md`, Nachtrag), Web-Dashboard-Programmkarte „MANUELL“ und Hero-Headline „Manueller Modus“ (siehe `docs/spec/17-webif-dashboard.md`, Nachtrag). Auf Hardware per `paho-mqtt` verifiziert: manuelle `V1/time/set`-Änderung bei aktivem Programm setzt `activeProgram` zuverlässig auf `0` zurück, **und** alle `V1`–`V5` `auto/state` gehen dabei auf `OFF` (Konsequenz aus der Wiederverwendung von `applyProgram(0)`) — siehe `docs/testing.md`.
