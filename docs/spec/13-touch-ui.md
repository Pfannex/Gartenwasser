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
- `src/MqttManager.h`, `src/MqttManager.cpp` (neue `requestMainCmd()`, wie in vorherigen Phasen nicht in der ursprünglichen Liste, aber notwendig; Nachtrag 2026-08-16: `requestProgramByShortcut()`, `requestProgramClear()`, Programm-Pflicht in `startSequence()`; Nachtrag 2026-08-17: beide durch `requestProgramSelect()` ersetzt, neue `requestValveCmd()`, `handleValveCmd()`-Kernlogik nach `applyValveCmd()` extrahiert)
- `src/ConfigStore.h/.cpp` (Nachtrag 2026-08-16: `getProgramIndexForShortcut()`; Nachtrag 2026-08-17: `getProgramIndexForShortcut()` und das `shortcut`-Feld wieder entfernt, `kMaxPrograms` 8→32)
- `src/main.cpp` (Nachtrag 2026-08-17: `SET_LOOP_TASK_STACK_SIZE` 32→64 KB)

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

## Nachtrag (2026-08-17): Touch-UI komplett neu gestaltet, `P1`–`P4` entfernt

Die Backlog-Idee oben (Untermenü-Struktur) wurde aufgegriffen und umgesetzt, aber im Zuschnitt angepasst: der „Timer“-Teil (Zeitplan-Bedienung am Display) wurde bewusst verworfen (kein sinnvoller Kalender auf 172×320px, Zeitplan-Einträge haben seit dem Entfernen des `name`-Felds ohnehin keinen sprechenden Titel mehr), stattdessen gibt es nur eine „Programme“-Unterseite. Auslöser war ein mehrstufiger Design-Dialog (Programm-Anzahl-Limit hinterfragt, 16-Ventil-Erweiterung angedacht dann auf eigene Phase verschoben, Web-Interface als eigenes Backlog-Thema abgegrenzt) sowie anschließend viele Runden Feintuning direkt am Gerät.

**Hauptseite** (`setupUi()`):
- Graue Titelzeile „Gartenwasser“ (rein statisch, wie eine Kopfzeile — keine Status-Funktion mehr, siehe Statuszeile unten).
- START/STOP-Toggle-Button, volle Breite, reduzierte Höhe — Beschriftung/Funktion unverändert zu vorher (`MqttManager::requestMainCmd()`), nur der Name „AUTO“→„START“ geändert.
- **Ventil-Statusmatrix**: 4×4 Anzeigefelder (`kMatrixCols`×`kMatrixRows`) statt der früheren vertikalen `lv_led`-Liste. `V0`–`V5` belegen zeilenweise die ersten 6 Zellen, die restlichen 10 bleiben als reine Platzhalter sichtbar (dünner grauer Rahmen, kein Füllstand) — Testaufbau für eine mögliche spätere Erweiterung auf 16 Ventile (volle MCP23017-Kapazität), die aber **nicht** Teil dieser Umsetzung ist (eigene, noch nicht begonnene Phase; betrifft u. a. `MqttManager::parseValveTopic()`, das aktuell einstellige Ventilnummern annimmt). Farblogik unverändert: grün = `auto`-ON + state-OFF, dunkelgrau = `auto`-OFF + state-OFF, rot = state-ON (überschreibt die anderen Fälle). Der früher zusätzliche gelbe Rahmen fürs aktive Sequenz-Ventil wurde entfernt (rot allein reicht als Hervorhebung).
  - `V1`–`V5` sind als `lv_btn` angelegt und **funktional**: Antippen schaltet das Ventil direkt per `V{n}/cmd` — neue `MqttManager::requestValveCmd(uint8_t index, bool on)`, ruft dieselbe Kernlogik wie `handleValveCmd()` auf (`applyValveCmd()`, aus dem bisherigen `handleValveCmd()` extrahiert), inkl. der bestehenden Sperre „manuelles ON während laufender Automatik wird ignoriert“. `V0` bleibt ohne Klick-Handler (hat wie bei MQTT keinen eigenen `cmd`, siehe V0-Kopplung).
- **Programme-Button**: direkt unter der Matrix, volle Breite. Zeigt als eigener Buttontext das aktuell aktive Programm (`refreshProgramsButtonLabel()`) — ersetzt die früheren `P1`–`P4`-Buttons komplett. Öffnet per Klick die neue Programme-Unterseite (`lv_scr_load()`, LVGL-Multi-Screen-Navigation — architektonisch neu für dieses Projekt).
- **Statuszeile** (zweizeilige Fußleiste, dunkelgrauer Hintergrund `0x333333` wie die Titelzeile, im Fehlerfall roter Hintergrund + gelbe Schrift statt nur roter Text): Zeile 1 in Priorität I2C-Fehler > transienter Programm-Hinweis (2 s, orange) > laufende Automatik-Sequenz „`V{n} mm:ss | mm:ss`“ (aktives Ventil | Restlaufzeit gesamt, gelb) > manuell (per Matrix-Tap) geschaltetes Ventil „MANUELL“ (hellblau) > sonst leer. Zeile 2 zeigt dazu jeweils den Alias-Namen des betroffenen Ventils.

**Programme-Unterseite** (`setupProgramScreen()`, neuer LVGL-Screen):
- Graue Menüzeile „Programm wählen“ oben (wie die Hauptseiten-Titelzeile).
- `<`/`>`-Buttons (ca. halbe Displaybreite, gut treffbar) blättern durch **alle** Programme inkl. eines virtuellen Eintrags „Kein Programm“ (Index 0) — startet beim Öffnen immer beim aktuell aktiven Programm.
- OK (wendet das durchgeblätterte Programm nur an, **startet nichts** — Start bleibt bewusst ein separater Schritt über START auf der Hauptseite) und Abbrechen (verwirft die Auswahl) — beide volle Breite, untereinander, unten bündig, grau statt grün/rot.
- Neue `MqttManager::requestProgramSelect(uint8_t programIndex)` (ersetzt `requestProgramByShortcut()`/`requestProgramClear()`) — wendet ein Programm per Index an (0 = abwählen), unabhängig von Shortcuts.

**`ConfigStore`**: `kMaxPrograms` 8→32 (nicht mehr auf 4 Touch-Buttons limitiert), `kProgramsJsonCapacity` 2048→8192. Das `shortcut`-Feld (Programme, `"P1"`–`"P4"`) wurde komplett entfernt — sein einziger Zweck war die `P1`–`P4`-Button-Bindung, mit deren Wegfall war es totes Konzept (Struct-Feld, JSON-Serialisierung/-Parsing, Duplikat-Prüfung in `setPrograms()`, `getProgramIndexForShortcut()` — alles entfernt statt nur ungenutzt liegen gelassen).

**`main.cpp`**: `SET_LOOP_TASK_STACK_SIZE` 32→64 KB (größeres `kProgramsJsonCapacity` ist jetzt der puffer-bestimmende Fall, vorher `schedule.json`).

Ausführlich interaktiv auf Hardware getestet und über viele Runden direkt am Gerät nachjustiert (Layout/Abstände/Textposition) — siehe `docs/testing.md`.

**Bewusst zurückgestellt / Backlog**:
- Web-Interface zur vollständigen Geräte-Konfiguration (komfortables Editieren von Programmen/Zeitplänen) — eigene, noch nicht begonnene Phase, Touch-UI bleibt bewusst nur für schnelle Vor-Ort-Bedienung zuständig.
- Erweiterung auf 16 Ventile (V0–V15) — eigene, noch nicht begonnene Phase (siehe oben).
- Feinschliff der Statuszeilen-/Button-Höhen („kann später nochmal hübsch gemacht werden“, Nutzer-Aussage) — funktional fertig, rein kosmetisch noch nicht final.

**Endgültig verworfen (2026-08-17)**: Touch-UI-Bedienung für den Zeitplan (Phase 15, „Timer“-Unterseite) — nicht nur zurückgestellt, sondern komplett gestrichen. Begründung (Nutzer): das 172×320px-Display ist dafür zu klein. Zeitplan-Bearbeitung bleibt vollständig dem geplanten Web-Interface vorbehalten.

### Nachtrag (2026-08-17): Nachjustierungen nach dem ersten Hardware-Test

Beim interaktiven Testen der Programme-Unterseite fielen vier Punkte auf, direkt behoben:

- Buttontext bei keinem gewählten Programm von „Kein Programm gewählt“ auf „Kein Programm“ gekürzt (passte nicht in den Button).
- **Verhaltensänderung**: `applyProgram(0)` („Kein Programm“ wählen) setzt jetzt zusätzlich alle `auto`-Flags (`V1`–`V5`) explizit auf `false` zurück — vorher blieb der `auto`-Zustand des zuletzt aktiven Programms an den Ventilen hängen, obwohl die Anzeige „Kein Programm“ zeigte (Nutzer-Feedback: „sonst bleibt das letzte Programm stehen“). Betrifft `MqttManager::applyProgram()`, gilt für Touch **und** MQTT (`main/program/cmd 0`).
- Programme-Button wird während einer laufenden Automatik-Sequenz gesperrt (`LV_STATE_DISABLED`, `LV_OBJ_FLAG_CLICKABLE` entfernt) — ein Programmwechsel mitten im Lauf hätte sonst zu inkonsistentem Zustand geführt („das gibt sonst Murks“).
- Alle Touch-Buttons (START, Ventilmatrix `V1`–`V5`, Programme, `<`/`>`/OK/Abbrechen) zeigen jetzt beim Antippen (`LV_STATE_PRESSED`) kurz Weiß als Trefferfeedback (`addPressHighlight()`-Helfer) — vorher war nicht erkennbar, ob ein Touch überhaupt registriert wurde.

### Nachtrag (2026-08-18): „Manueller Modus" statt „Kein Programm", START-Button gesperrt

Im Zuge der „MANUELL"-Konsistenz-Überarbeitung (siehe `docs/spec/14-programme.md`, Nachtrag, und `docs/spec/18-webif-konfiguration.md`, Nachtrag): eine manuelle `time`/`auto`-Änderung setzt `activeProgram` jetzt auf 0 zurück (statt wie bisher nur direkt nach dem Boot), der Programme-Button zeigt diesen Zustand also nicht mehr nur beim ersten Start, sondern potenziell jederzeit während des Betriebs — die bisherige Beschriftung „Kein Programm" (rein informativ) wurde deshalb auf **„Manueller Modus"** geändert (`refreshProgramsButtonLabel()`), passend zur bereits bestehenden Statuszeilen-Beschriftung „MANUELL" und zum gleichzeitig überarbeiteten Web-Dashboard.

**Verhaltensänderung**: `refreshMainButton()` sperrt den START-Button jetzt (`LV_STATE_DISABLED` + `LV_OBJ_FLAG_CLICKABLE` entfernt), solange kein Programm gewählt ist und keine Sequenz läuft — vorher blieb der Button aktiv und zeigte nur einen 2 Sekunden langen Hinweis „Kein Programm vorgewählt!“, wenn der Start serverseitig ohnehin abgelehnt wurde (`MqttManager::startSequence()`, unverändert). STOP bleibt davon unberührt (Running-Zustand hat Vorrang vor dem Programm-Check), damit eine während einer laufenden Sequenz durch `main/config/set` ausgelöste MANUELL-Rücksetzung den Abbruch nicht blockiert. Der bisherige transiente Hinweis-Mechanismus (`programHintText`/`programHintUntilMs`/`kProgramHintDurationMs`) wurde komplett entfernt statt nur ungenutzt gelassen — er wird durch die Button-Sperre gegenstandslos, da `mainButtonEventHandler` im gesperrten Zustand gar nicht mehr feuert.

Auf Hardware getestet und vom Nutzer bestätigt („schaut gut aus, erledigt“).
