# Phase 13 — Touch-UI (Automatik-Toggle & Statusanzeige)

**Status:** ✅ Erledigt & getestet

## Ziel

Lokale Bedienung/Anzeige direkt am Gerät, ergänzend zur MQTT/Home-Assistant-Steuerung.

## Voraussetzungen

- Phase 4 (Ventile per MQTT) ✅ — Statusanzeige braucht die Ventil-States
- Phase 7 (Automatik-Sequenz) ✅ — Toggle-Button braucht `main/cmd`/`main/state`

## Umsetzung

- Erweiterung von `HmiManager` (`src/HmiManager.h/.cpp`), löst den ursprünglichen Platzhalter-Screen ab:
  - Toggle-Button „AUTO“/„OFF“, gekoppelt an `main/cmd`/`main/state` (Touch löst denselben Pfad aus wie ein MQTT-`cmd`, keine Sonderlogik).
  - Ventile `V0`–`V5` als runde Status-Indikatoren (`lv_led`, radio-button-artig): grün = AUS, rot = AN. Ventile mit `auto=OFF` werden im AUS-Zustand dunkelgrau gedimmt (nicht Teil der Automatik-Sequenz), springen bei manueller Einschaltung aber normal auf Rot. Aktives Sequenz-Ventil wird zusätzlich gelb hervorgehoben.
  - Statuszeile als Fußleiste unten (dunkelgrauer Hintergrund, volle Breite/verbleibende Höhe): zeigt priorisiert an, was gerade passiert — I2C-Fehler > transienter Hinweis (2s, z. B. „Kein Programm vorgewählt!“/„P3 nicht konfiguriert!“) > laufende Automatik (Alias/Ventilname + Restlaufzeit) > manuell laufende Ventile (+ Anzahl weiterer) > gewähltes Programm (Name) > „MANUELL“ (kein Programm gewählt).
  - 4 Toggle-Buttons „P1“–„P4“ rechts (Radio-Verhalten, nur einer gleichzeitig aktiv, siehe Nachtrag unten) — wenden das Bewässerungsprogramm an, dessen `shortcut`-Feld dem Button entspricht (Phase 14).
- `HmiManager` liest den Zustand direkt aus `ValveController`/`Sequencer`/`Diagnostics`/`ValveTimer` (kein MQTT-Umweg für die lokale Anzeige).
- `MqttManager::requestMainCmd(bool)` (neu, öffentlich) kapselt den Aufruf von `startSequence()`/`stopSequence()` — von MQTT-`main/cmd` und Touch gleichermaßen genutzt, keine doppelte Logik.
- Alias-Texte mit Umlauten werden für die Display-Anzeige lokal auf ASCII transliteriert (ä→ae, ö→oe, ü→ue, ß→ss) — die eingebauten LVGL-Fonts enthalten keine Umlaute. Die eigentlichen Alias-Werte (MQTT/`ConfigStore`) bleiben unverändert UTF-8.

## Betroffene Dateien

- `src/HmiManager.h`, `src/HmiManager.cpp`
- `src/MqttManager.h`, `src/MqttManager.cpp` (neue `requestMainCmd()`, wie in vorherigen Phasen nicht in der ursprünglichen Liste, aber notwendig; Nachtrag 2026-08-16: `requestProgramByShortcut()`, `requestProgramClear()`, Programm-Pflicht in `startSequence()`)
- `src/ConfigStore.h/.cpp` (Nachtrag 2026-08-16: `getProgramIndexForShortcut()`)

## Test

1. Touch auf „AUTO“ → Automatik-Sequenz startet, identisch zu `main/cmd ON` per MQTT (auch `main/state`/`activeValve` in HA aktualisieren sich).
2. Touch auf „OFF“ während laufender Automatik → Sequenz stoppt sofort, wie bei `main/cmd OFF` per MQTT.
3. Ventil manuell per MQTT schalten → Statusanzeige auf dem Display aktualisiert sich.
4. Während Automatik-Lauf: aktives Ventil wird auf dem Display sichtbar hervorgehoben.

## Test / Ergebnis

- Alle vier Testfälle auf Hardware verifiziert, vom Nutzer bestätigt ("sieht klasse aus").
- Iterativ verfeinert: LED-Statusindikatoren statt Text-ON/OFF, Dimmen bei `auto=OFF`, Umlaut-Transliteration, Statuszeile als abgesetzte Fußleiste, P1–P4-Platzhalter-Buttons mit Radio-Verhalten.

## Nachtrag (2026-08-16, Phase 14): `P1`–`P4`-Buttons an Bewässerungsprogramme angebunden

- Button-Druck ruft `ConfigStore::getProgramIndexForShortcut()` auf (liest das `shortcut`-Feld der Programme, siehe `docs/spec/14-programme.md`) und wendet das gefundene Programm über `MqttManager::requestProgramByShortcut()` an — kein MQTT-Umweg, analog zu `requestMainCmd()`.
- **Toggle-Verhalten**: ist das dem Button zugeordnete Programm bereits aktiv, wählt ein erneuter Druck es wieder ab (`MqttManager::requestProgramClear()`, identisch zu `main/program/cmd 0`) — konsistent mit dem Boot-Grundzustand („nichts gewählt“ ist ein regulärer, erreichbarer Zustand, nicht nur ein Startwert).
- Ist einem Button kein Programm zugeordnet (`shortcut` bei keinem Programm gesetzt), wird nichts angewendet, sondern ein transienter Hinweis „P{n} nicht konfiguriert!“ (2 s, orange) eingeblendet.
- Der sichtbare Checked-Status der Buttons wird **nicht** beim Klick lokal gesetzt, sondern periodisch (`refreshProgramButtons()`, alle 250 ms) aus `ConfigStore::getActiveProgram()` abgeleitet — bleibt dadurch auch korrekt, wenn die Programmwahl über MQTT (`main/program/cmd`, `main/programs/set`) geändert wird, nicht nur über Touch. Ist ein Programm ohne Button-Bindung aktiv (z. B. Programm 5–8 der bis zu 8 möglichen), leuchtet konsequent keiner der Buttons — die Statuszeile zeigt den Programmnamen trotzdem an.
- Zusätzlich, im selben Zug umgesetzt: `main/cmd ON` (Touch **und** MQTT) startet nur noch, wenn ein Programm gewählt ist — siehe `docs/spec/07-automatik-sequenz.md`, Nachtrag. Ohne Programm zeigt der Touch-`AUTO`-Button einen Hinweis „Kein Programm vorgewählt!“ (2 s, orange), startet aber nichts.
- Getestet interaktiv auf Hardware (Nutzer bedient Display, Ergebnis per Live-MQTT-Mitschnitt/gezielten Abfragen gegengeprüft) — siehe `docs/testing.md`.

## Backlog-Idee (2026-08-16): Untermenü-Struktur statt fester Buttons

Noch nicht eingeplant, reine Notiz für später — aktuell begrenzen feste `P1`–`P4`-Buttons die Touch-Auswahl auf 4 gebundene Programme (von bis zu 8 möglichen), und es gibt noch keine Display-Bedienung für den Zeitplan (Phase 15, „Timer“). Idee: Hauptseite mit Untermenüs statt alles auf einer Seite:

- **Hauptseite**
  - Button „Programme“ (→ Untermenü)
  - Anzeige aktives Programm
  - Button „Timer“ (→ Untermenü)
  - Anzeige aktiver Timer
- **Untermenü „Programme“**
  - Buttons „<“/„>“ zum rollierenden Durchblättern aller Programme (nicht auf 4 begrenzt)
  - Anzeige Programmname
  - Buttons „OK“/„Abbrechen“
- **Untermenü „Timer“** (Zeitplan, Phase 15)
  - Buttons „<“/„>“ zum rollierenden Durchblättern der Zeitplan-Einträge
  - Anzeige Eintragsname
  - Buttons „OK“/„Abbrechen“

Setzt Phase 15 (Zeitplan) voraus, um sinnvoll zu sein („Timer“-Untermenü braucht `schedule`-Einträge). Würde die aktuelle, mit Phase 13/14 gebaute Ein-Seiten-Struktur (feste Buttons, permanente Statuszeile) grundlegend ersetzen — bei Umsetzung gegen den dann aktuellen Funktionsumfang neu bewerten, nicht blind übernehmen.
