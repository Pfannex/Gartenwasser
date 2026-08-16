# Log — Gartenwasser

Chronologisches Entwicklungs-Log. Fachliche Spezifikation siehe [requirements.md](requirements.md), Phasen-Details siehe [spec/](spec/).

## 2026-08-14

### PlatformIO-Library-Cache defekt

`.pio/libdeps/esp32-c6-devkitc-1` war beschädigt (u.a. `GFX Library for Arduino` ohne `library.properties`/`src/`, `lvgl`/`PubSubClient`/`ArduinoJson` fehlten komplett) → `MissingPackageManifestError` beim Build. Fix: kompletten `.pio`-Ordner gelöscht (reiner Build-/Dependency-Cache, in `.gitignore`), PlatformIO installiert Abhängigkeiten beim nächsten Build sauber neu.

### Phase 2 — MQTT-Grundgerüst umgesetzt

- `MqttManager` (`src/MqttManager.h/.cpp`, neu): PubSubClient-Verbindung zum Broker aus `secrets.h`, LWT auf `gartenwasser/availability` = `offline` (retained, QoS 1), nach Connect `online` (retained). Nicht-blockierendes Reconnect-Handling analog zu `WifiManager` (15 s Intervall).
- In `main.cpp` eingebunden (`begin()` in `setup()`, `loop()` in `loop()`).

### Boot-Ablauf: WLAN + NTP blockierend, Logger auf Echtzeit

- `WifiManager::connectAndSyncTimeBlocking()` (neu): baut beim Boot zuerst die WLAN-Verbindung auf (Timeout 20 s), direkt danach NTP-Synchronisierung (`pool.ntp.org` / `de.pool.ntp.org`, Zeitzone Europe/Berlin, Timeout 10 s). Beide Wait-Routinen inkl. Timeouts/Konstanten liegen vollständig in `WifiManager.cpp`, damit `main.cpp` ein schlanker Orchestrator bleibt.
- Bei Erfolg schaltet `Logger::enableRealTime()` das Log-Zeitformat von boot-relativer Zeit (`millis()`-basiert) auf Echtzeit um (`hh:mm:ss` aus NTP, `mmm` weiterhin aus `millis()`). Schlägt WLAN oder NTP fehl, läuft der Boot mit Fehlermeldung und boot-relativer Zeit weiter (kein Hänger ohne Netz).
- `Logger::Source::SYSTEM` ergänzt (`"SYS  "`) für Boot-/Systemmeldungen wie `"Setup abgeschlossen."`.

### Getestet auf Hardware

- Geflasht per PlatformIO „Upload and Monitor“ (VS Code Task, Shortcut `Strg+Alt+U`) — läuft.

### Doku-Status nachgezogen, Git-Historie bereinigt

- Phase-2-Status in `docs/README.md`, `docs/spec/02-mqtt-grundgeruest.md` und `docs/requirements.md` auf ✅ nachgezogen.
- Lokaler `master` hatte durch ein `git commit --amend` nach dem ersten Push keine gemeinsame Historie mehr mit `origin/master` (zwei unabhängige Root-Commits). Fix: `git reset --soft origin/master`, alle Änderungen in einem neuen Commit zusammengefasst, normal gepusht (kein Force-Push nötig).
- `.claude/settings.local.json`: `git push` als Permission-Regel hinterlegt, läuft seither ohne Rückfrage.

### Phase 3 — ValveController umgesetzt

- `ValveController` (`src/ValveController.h/.cpp`, neu): `setValve(index, on)`/`getValve(index)` für V0–V5, schreibt auf MCP23017 Port B (`I2CManager::writeRegister()`) mit der Pinbelegung aus `requirements.md` (V0=B7, V1=B2…V5=B6). `begin()` setzt beim Boot alle Ventile auf AUS.
- `main.cpp`: `ValveController::begin()` in `setup()` eingebunden. Temporärer Serial-Testhandler (`v0on`…`v5on`/`v0off`…`v5off`) ergänzt — entfällt wieder mit Phase 4 (MQTT-Anbindung der Ventile).
- Getestet auf Hardware: alle sechs Ventile einzeln per Serial-Befehl geschaltet, Relais-Ausgänge laufen einwandfrei.

### Phase 4 — Ventile per MQTT umgesetzt

- `MqttManager`: abonniert `gartenwasser/V{1..5}/cmd` nach jedem (Re-)Connect, Callback parst `ON`/`OFF` und schaltet über `ValveController`. `state` (inkl. `V0`) wird retained published, nur bei tatsächlicher Änderung. Nach jedem Connect werden zusätzlich alle aktuellen Ventilzustände neu published (Resilienz bei Reconnect, siehe `requirements.md`).
- V0-Kopplung: `Vn ON` schaltet `V0` mit ein; `Vn OFF` schaltet `V0` nur aus, wenn kein anderes Ventil mehr aktiv ist (`ValveController::anyIrrigationValveActive()`, neu).
- Ungültige Topics/Payloads werden geloggt und ignoriert.
- `main.cpp`: temporärer Serial-Testhandler aus Phase 3 entfernt (durch MQTT abgelöst).
- Getestet auf Hardware: alle Ventile per MQTT einzeln/kombiniert geschaltet, V0-Kopplung verifiziert.

## 2026-08-15

### Phase 5 — Laufzeit & Restlaufzeit umgesetzt

- `ConfigStore` (`src/ConfigStore.h/.cpp`, neu): persistiert `time` je Ventil und `maxTime` in `/config.json` auf SPIFFS (ArduinoJson v6). Defaults: 5 Min./Ventil, 60 Min. `maxTime`.
- `ValveTimer` (`src/ValveTimer.h/.cpp`, neu): reiner Countdown — `start()` setzt `remaining = min(time, maxTime)`, `tick()` zählt sekündlich herunter, meldet abgelaufene Ventile über eine Bitmaske zurück.
- `MqttManager`: `V{1..5}/time/set` (validiert, Fehler werden geloggt statt übernommen), `time/state`+`time/remaining` (nicht retained) published, `main/time/maxTime` retained published. `applyValveCommand` steuert den Timer mit (Start bei ON, Reset bei OFF).
- **Wichtig:** der sekündliche Countdown/die Auto-Abschaltung läuft bewusst **vor** dem WLAN-Verbindungscheck in `MqttManager::loop()`, also unabhängig von WLAN/MQTT — sonst würde ein Verbindungsausfall den Countdown einfrieren und ein Ventil könnte unbegrenzt offen bleiben (Anforderung „läuft lokal/autonom weiter“ aus `requirements.md`). In der Phase-5-Spec nicht explizit erwähnt, aber zwingend notwendig.
- `main/time/maxTime` ist vorerst nur publish-only (Default 60 Min.) — der Set-Weg (`main/time/set`-JSON) kommt laut Spec erst in Phase 11.
- Getestet auf Hardware: `time/set` übersteht Neustart (Persistenz bestätigt), Ventile schalten bei `00:00` automatisch ab.

### Phase 6 — Automatik-Flag umgesetzt

- `ConfigStore`: `auto`-Werte je Ventil (bool, Default `false`) persistiert in `/config.json`, analog zu `time`.
- `ValveController::getAuto()`/`setAuto()` als dünne Facade über `ConfigStore`.
- `MqttManager`: `V{1..5}/auto/set` (ON/OFF, gemeinsamer Payload-Parser mit `cmd`) → `auto/state` retained, republished nach jedem Connect.
- Bug gefunden und gefixt: `publishAutoState()` nutzte `char topic[24]` — zu klein für `"gartenwasser/V1/auto/state"` (27 Byte inkl. Nullterminator), `snprintf` hat den Topic-String stillschweigend auf `.../auto/st` abgeschnitten. Puffer auf 32 Byte vergrößert.
- Getestet auf Hardware: `auto/set` schaltet `auto/state` retained, übersteht Neustart.

### Logging: PUB/SUB, Spaltenreihenfolge, Timing-Fixes

- `Logger`: `Type` um `PUB`/`SUB` erweitert; Zeilenformat von `TYPE CLASS` auf `CLASS TYPE` gedreht (z. B. `MQTT PUB`, `I2C INFO`) statt `TYPE CLASS`.
- `MqttManager`: zentrale `publishAndLog()`-Stelle für alle ausgehenden Publishes (loggt `Type::PUB`), jede eingehende Nachricht wird in `handleMqttMessage()` als `Type::SUB` geloggt — auch bei unbekannten Topics/ungültigen Payloads.
- Bug gefunden und gefixt: erster MQTT-Verbindungsversuch wartete bis zu 15 s **seit Boot** statt sofort nach WLAN-Connect, weil `lastAttemptMs` bei `0` startete und derselbe Reconnect-Intervall-Check auch den allerersten Versuch gated hat (`MqttManager::begin()` startet — anders als `WifiManager::begin()` — keinen aktiven Verbindungsversuch). Fix: `lastAttemptMs` auf `0 - kReconnectIntervalMs` initialisiert, damit der erste Check sofort durchgeht.
- Bug gefunden und gefixt: `"Verbunden."` wurde erst einen `loop()`-Tick nach den ganzen Reconnect-Publishes geloggt (späte `wasConnected`-Erkennung). Jetzt direkt in `connectToBroker()` geloggt, noch vor den Publishes.
- Debug-Rest aus der Bring-up-Phase entfernt: `esp_lcd_touch_axs5106l.cpp` gab beim Touch-Init roh `"read: ..."` auf `Serial` aus (kein `Logger`, keine Zeitstempel) — Block samt ungenutztem ID-Register-Read entfernt.
- `main.cpp`: Leerzeile + Trennlinie (`---...`, 60 Zeichen) vor jedem Boot auf `Serial`, damit im Monitor klar erkennbar ist, wo ein Neustart beginnt.

### Doku: RETAIN-Spalte, main/config/set statt main/reset

- MQTT-Topic-Struktur in `requirements.md` um eine `RETAIN`-Spalte ergänzt (gilt für alle Topics, nicht nur die vom Device gesendeten).
- Diskutiert: eigenes `main/reset`-Topic für Tests/Betrieb (alle Ventile aus, Werte auf Vorgabe). Verworfen zugunsten von `main/config/set`/`main/config/state` (volle Konfiguration als JSON, lesen/schreiben, Teil-Updates) — deckt den Reset-Anwendungsfall über extern gespeicherte JSON-Payloads ab, ohne Firmware-seitige Presets, und ersetzt gleichzeitig die für Phase 11 geplanten `main/time/set`/`main/auto/set`-Sammelbefehle. `docs/spec/11-sammelbefehle.md` entsprechend neu geschrieben. Noch nicht implementiert.

### Backlog: Bewässerungsprogramme (Phase 14) + Wochenplan (Phase 15)

- Neue Idee: mehrere benannte Presets (`time` **und** `auto` je Ventil, z. B. `SHORT`/`MEDIUM`/`LONG`/`TEST`) als Array in `config.json`, auswählbar per `main/program/cmd <integer>`, `main/program/state` zeigt den aktiven Index (Phase 14, `docs/spec/14-programme.md`). `auto` bewusst mit im Programm, um z. B. „nur Rasen, nicht die Beete" abzubilden — Auswahl überschreibt einmalig, kein dauerhaftes Lock.
- Als Ausblick weit hinten im Backlog: ein Wochenplan/Scheduler (Phase 15, `docs/spec/15-wochenplan.md`, „on top" auf Sequencer + Programme), der pro Wochentag automatisch ein Programm anwendet und `main/cmd ON` zu einer konfigurierten Uhrzeit auslöst. Bewusst grob/unentschieden dokumentiert (offene Fragen: Startzeit pro Tag oder global, Verhalten bei verpasstem Trigger, ...) — Zweck ist, dass die Umsetzung von Phase 7/14 nicht in eine Richtung läuft, die das später erschwert.

### Phase 7 — Automatik-Sequenz umgesetzt

- `Sequencer` (`src/Sequencer.h/.cpp`, neu): reine Warteschlange + Cursor (kein Ventil-/MQTT-Wissen), analog zu `ValveTimer`.
- `MqttManager`: `main/cmd ON` baut die Warteschlange aus allen Ventilen mit `auto=ON` (V1→V5) und startet das erste; `main/cmd OFF` schaltet das aktive Ventil aus und resettet. `main/state`/`main/activeValve` (retained), `main/remainingTotal` (sekündlich, nicht retained) published.
- Manuelles `V{n}/cmd ON` wird während der Automatik ignoriert; manuelles `V{n}/cmd OFF` des aktiven Ventils wird angenommen und rückt die Sequenz sofort weiter — identisch zum Zeitablauf-Fall (beide Pfade über `advanceSequence()`).
- Getestet auf Hardware: Sequenzverlauf, vorzeitiges Weiterschalten per manuellem OFF, Abbruch per `main/cmd OFF` — alles wie spezifiziert.

### Retained Broker-Leichen bereinigt

- Alte truncated Topics (`gartenwasser/V{n}/auto/st`, vom Buffer-Bug aus Phase 6) waren noch als retained Messages auf dem Broker vorhanden, obwohl der Code-Fix schon lief. Per kurzem Python/paho-mqtt-Skript alle 23 retained Topics unter `gartenwasser/#` ermittelt und gelöscht (leerer retained Payload je Topic) — Board publiziert die korrekten Werte beim nächsten (Re-)Connect automatisch neu.

### Restlaufzeit-Semantik verfeinert: idle = time, aber nicht waehrend laufender Sequenz

- Ursprünglicher Phase-5-Nachtrag (idle Ventil zeigt `remaining = time` statt `00:00`, siehe oben) hatte eine Lücke: waehrend eine Automatik-Sequenz noch läuft, sahen bereits durchgelaufene Ventile durch das Re-Armieren wieder wie "noch an der Reihe" aus.
- Fix: `ValveTimer` bekommt `clear(index)` (Restlaufzeit auf 0) zusaetzlich zu `reset(index)` (auf konfigurierte Zeit). `applyValveCommand`s OFF-Zweig setzt jetzt immer erst `clear()`; die Aufrufer entscheiden: Ventil war aktives Sequenz-Ventil → bleibt 0 bis Sequenz-Ende (`advanceSequence()`), sonst sofort `armIdleValve()` (zurück auf `time`). Sequenz-Ende (Warteschlange erschöpft oder `main/cmd OFF`) → `armAllValves()`, alle fünf Ventile zurück auf `time` (deckt sich mit der Original-Anforderung in `requirements.md`).
- `handleTimeSet()`s Idle-Refresh greift jetzt nur noch, wenn keine Sequenz läuft — sonst würde ein `time/set` auf ein bereits durchgelaufenes Ventil es faelschlich wieder auf "time" (= "noch an der Reihe") springen lassen.
- Getestet auf Hardware, vom Nutzer bestätigt ("passt jetzt").

### Backlog-Erweiterung: Phase 15 von Wochenplan zu generischem Zeitplan/Scheduler

- Tages- und Wochenplan sind keine getrennten Features, sondern beides Trigger-Typen (`daily`/`weekly`/`once`) desselben Zeitplan-Mechanismus: eine beliebig lange, über `main/config/set` editierbare Liste von Einträgen (Trigger-Regel + Programm-Referenz aus Phase 14), nicht auf 7 feste Wochentags-Slots begrenzt. Deckt „jeden Tag 21:00 Uhr", „jeden Dienstag" und „genau am 01.02.26, 11:00 Uhr" ab.
- Zusätzliche offene Fragen ergänzt: Umgang mit einmaligen (`once`-)Triggern nach dem Feuern, Konflikte bei zeitgleichen Triggern. `docs/spec/15-wochenplan.md` entsprechend erweitert (Dateiname unverändert gelassen, um Verweise nicht zu brechen).

### Phase 8 — Diagnostics umgesetzt

- `Logger`: `currentTimestamp()` öffentlich gemacht, neuer `setErrorCallback()` — jeder `Type::ERROR`-Aufruf ruft optional einen registrierten Callback mit der Meldung auf (lose Kopplung per Funktionszeiger, `Logger` kennt `Diagnostics` nicht).
- `I2CManager`: `isMcp23017Reachable()` als wiederverwendbare Prüfung ergänzt.
- `Diagnostics` (`src/Diagnostics.h/.cpp`, neu): registriert sich als Logger-Error-Callback (→ `lastError` inkl. Zeitstempel aus `Logger::currentTimestamp()`), prüft periodisch `i2cStatus` über `I2CManager`.
- `MqttManager`: `diagnostics/i2cStatus`/`diagnostics/lastError` (beide retained) bei Änderung published, republished nach jedem Connect. Check läuft wie der Ventil-Timer-Tick unabhängig von WLAN/MQTT.
- `main.cpp`: `Diagnostics::begin()` ganz am Anfang von `setup()`, vor `ConfigStore::begin()`, damit auch frühe Boot-Fehler (SPIFFS-Mount, WLAN-Timeout) erfasst werden.
- Clou: I2C-Ausfall, ungültige MQTT-Payloads und WLAN-Verlust landen automatisch in `lastError`, ohne bestehende `ERROR`-Log-Aufrufe in `WifiManager`/`MqttManager`/etc. anzufassen.
- Getestet auf Hardware, vom Nutzer bestätigt ("klappt!!").
- Nebenbei nachgezogen: `Sequencer`-Zeile in der Architektur-Tabelle (`requirements.md`) war noch nicht auf ✅ aktualisiert; Log-Format-Beispiel (`TYPE CLASS` statt `CLASS TYPE`, fehlendes `PUB`/`SUB`/`SYS`) war veraltet.

### Phase 9 — Alias je Ventil umgesetzt (inkl. V0-Nachtrag)

- `ConfigStore`: `alias`-Werte je Ventil (max. 32 Zeichen, Default leer) persistiert in `/config.json`, JSON-Puffer auf 768 Byte erhöht (Alias-Texte werden beim Laden kopiert, nicht nur referenziert).
- `ValveController::getAlias()`/`setAlias()` als Facade.
- `MqttManager`: `V{n}/alias/set` → `V{n}/alias` (retained, bewusst **kein** `/state`-Suffix wie in der Spec). Validierung: max. 32 Zeichen und keine Steuerzeichen (Bytes < 0x20), UTF-8/Umlaute erlaubt.
- Bug gefunden und gefixt: Längenprüfung lief zunächst gegen den bereits von `copyPayload()` gekürzten String — ein zu langer Alias wäre still gekürzt statt abgelehnt worden. Fix: Validierung nutzt die ursprüngliche Payload-Länge aus dem PubSubClient-Callback.
- Nachtrag auf Nutzerwunsch: `V0` (Hauptventil) hatte noch keinen Alias (Spec war auf `V1`–`V5` begrenzt). `ConfigStore::getValveAlias()`/`setValveAlias()` erlauben jetzt Index `0..5`; `V0/alias/set` wird in `MqttManager` separat behandelt (nicht über `parseValveTopic()`, das bewusst auf `V1..V5` begrenzt bleibt, da `V0` kein `cmd`/`time`/`auto` hat).
- Getestet auf Hardware, vom Nutzer bestätigt ("klapp!").

### Priorisierung angepasst: Phase 10 (HA-Discovery) ans Ende gestellt

- Auf Wunsch: erst alles geräteintern fertigstellen, bevor die erste externe Integration (Home Assistant) angegangen wird. Neue Bearbeitungsreihenfolge: 11 (Konfiguration per JSON) → 12 (Aufräumen) → 13 (Touch-UI) → 14 (Programme) → 15 (Zeitplan/Scheduler) → 10 (HA-Discovery, ganz am Ende).
- Phasennummern/Dateinamen bleiben unverändert, nur die Reihenfolge in `docs/README.md` wurde angepasst (Tabellenreihenfolge = Bearbeitungsreihenfolge, nicht mehr die Nummer). Begründung in `requirements.md`, Entscheidungshistorie.

### Phase 11 — Konfiguration per JSON umgesetzt

- `ConfigStore::toJson()`: serialisiert die komplette Konfiguration, nutzt intern dieselbe `buildJson()`-Struktur wie `save()` (SPIFFS) — kein doppelt gepflegtes Schema.
- `MqttManager`: `main/config/set` (JSON, Teil-Updates) und `main/config/state` (retained, JSON). Die einzelnen `V{n}/time/set`/`auto/set`/`alias/set`-Handler wurden in reine `apply*Value()`-Kernfunktionen (Validierung + Persistierung + Publish, ohne Payload-Parsing) und dünne `handle*()`-Wrapper (parsen den MQTT-String-Payload) aufgeteilt — `handleConfigSet()` ruft dieselben `apply*Value()`-Funktionen direkt mit den JSON-Werten auf, keine doppelte Validierung.
- `maxTime` ist jetzt zum ersten Mal setzbar, ausschließlich über `main/config/set` (kein eigenes `main/time/set`, wie geplant).
- `main/config/state` wird nach jeder Konfigurationsänderung neu publiziert, egal über welches Topic sie kam (auch die Einzel-Topics lösen es aus), sowie nach jedem (Re-)Connect.
- PubSubClient-Puffer (Default 256 Byte) reicht nicht für die volle Konfiguration inkl. aller Aliase als JSON → `setBufferSize(1024)` in `MqttManager::begin()`.
- Doku: vollständiges Beispiel (alle Ventile, `time`/`auto`/`alias` inkl. V0, `maxTime`) in `docs/spec/11-sammelbefehle.md` ergänzt.
- Alle vier Testfälle aus der Spec auf Hardware verifiziert.

### Phase 12 — Aufräumen/Refactoring (Code-Review-Pass)

- Veraltete Kommentare korrigiert: `main.cpp`s Datei-Header (nannte nur 3 von 11 Klassen) und `ConfigStore::begin()`-Kommentar (\"später auto, alias\" — beides längst fertig).
- `ConfigStore::kJsonCapacity` (768) öffentlich gemacht, `MqttManager`s eigene doppelte `768`-Konstante entfernt und ersetzt.
- `MqttManager.cpp`: individuell geschätzte Topic-Puffer (`char topic[24]`/`[32]`, ca. ein Dutzend Stellen) durch eine gemeinsame, großzügige `kTopicBufferSize = 48` ersetzt — genau aus so einer Einzelgröße kam schon einmal der `auto/state`-Truncation-Bug.
- Geprüft und als bereits erledigt markiert: „MqttManager/Diagnostics/Logger koppeln" war noch als offener Punkt gelistet, ist aber seit Phase 8 fertig.
- Bewusste Entscheidung **gegen** eine zentrale Topic-Präfix-Konfiguration (Aufwand/Risiko vs. rein hypothetischem Nutzen bei einem Einzelgerät-Hobbyprojekt) — mit Begründung in `docs/spec/12-aufraeumen.md` dokumentiert, damit die Entscheidung nachvollziehbar bleibt statt einfach zu verschwinden.
- Speicher-Check dokumentiert: RAM 33,6 % (110.108/327.680 Byte), Flash 41,0 % (1.288.653/3.145.728 Byte) — nach Build ohne Regression.
- 10-Punkte-Test-Checkliste für künftige Firmware-Updates in `docs/spec/12-aufraeumen.md` ergänzt (Boot, Ventile, Laufzeit, Auto-Flag, Sequenz, Diagnostics, Alias, Config-JSON, Resilienz, Persistenz).

### Phase 13 — Touch-UI umgesetzt

- `MqttManager::requestMainCmd(bool)` (neu, öffentlich): kapselt `startSequence()`/`stopSequence()` — von MQTT-`main/cmd` und Touch gleichermaßen genutzt, keine doppelte Logik.
- `HmiManager`: Platzhalter-Screen ersetzt durch AUTO/OFF-Toggle-Button, Ventile `V0`–`V5` als `lv_led`-Statusindikatoren (grün=AUS, rot=AN), Statuszeile als dunkelgraue Fußleiste unten (Fehler > Automatik > manueller Betrieb > gewähltes Programm > „Bereit“, priorisiert und farbcodiert), 4 Platzhalter-Buttons „P1“–„P4“ (Radio-Verhalten) für Phase 14.
- Iterativ verfeinert auf Nutzer-Feedback: Ventile mit `auto=OFF` werden im AUS-Zustand dunkelgrau gedimmt (springen bei manueller Einschaltung normal auf Rot); Alias-Anzeige aus der kompakten Ventil-Liste wieder entfernt (Platz für P1–P4), aber in der Statuszeile für das aktive/laufende Ventil wieder aufgenommen; Umlaute im eingebauten LVGL-Font nicht darstellbar → lokale ASCII-Transliteration (nur Anzeige, MQTT/`ConfigStore` bleiben UTF-8).
- Getestet auf Hardware, vom Nutzer bestätigt ("sieht klasse aus").

## Offene Punkte / nächste Schritte

### Phase 14 — Design fuer Bewaesserungsprogramme abgestimmt

- `programs`-Array + `activeProgram` als Konfigurationsbereich analog zu Phase 11 (später am selben Tag auf eigene Datei/Topics umgestellt, siehe nächster Eintrag).
- 1-basierte Nummerierung passend zu `P1`-`P4`/`V1`-`V5`, `0` = kein Programm gewaehlt (Startwert und explizit setzbar via `main/program/cmd 0`).
- `time`/`auto` je Programm sind Teilmengen mit identischer Semantik wie `main/config/set` (enthaltene Felder werden uebernommen, fehlende bleiben unveraendert) - bewusst keine Sonderregel fuer `auto`, ein Regelwerk fuer alles ist einfacher.
- Anwenden eines Programms ruft dieselben `applyTimeValue()`/`applyAutoValue()`-Kernfunktionen wie `main/config/set` auf (Phase-11-Architektur zahlt sich hier direkt aus) - keine neue Validierungslogik.
- `maxTime`/`alias` bewusst kein Teil eines Programms.
- Obergrenze 8 Programme (`ConfigStore::kMaxPrograms`), Programmname nutzt `kAliasMaxLength` mit. `ConfigStore::kJsonCapacity` muss von 768 auf ca. 2048 Byte steigen, MQTT-Puffer entsprechend mit.
- Vollstaendiges Beispiel in `docs/requirements.md` und `docs/spec/14-programme.md` ergaenzt.
- Anbindung der Touch-UI-Buttons `P1`-`P4` (Phase 13) folgt als eigener Schritt **nach** der Config selbst - noch nicht umgesetzt.

### Konfiguration in drei Bereiche aufgeteilt — config/programs/schedule

- Auf Nutzerwunsch geprüft, ob eine gemeinsame `config.json` für laufende Ventilparameter, Programme (Phase 14) und Zeitplan (Phase 15) sinnvoll ist — Antwort: nein, stattdessen drei eigene Bereiche, weil sie unterschiedlich oft/aus unterschiedlichen Gründen geändert werden und eine gemeinsame Struktur mit jeder Phase weiter gewachsen wäre (Puffer-/JSON-Capacity für alle drei zusammen).
- `config` (`/config.json`, `main/config/set`/`state`): bleibt bei `time`/`auto`/`alias`/`maxTime`, unverändert zu Phase 11.
- `programs` (`/programs.json`, `main/programs/set`/`state` für Bulk-Editieren): Programme-Array + `activeProgram` (gehört inhaltlich zu den Programmen, nicht zur Config) — das schlanke `main/program/cmd`/`state` (Singular) zur Index-Auswahl bleibt zusätzlich bestehen, ruft intern dieselbe Logik auf.
- `schedule` (`/schedule.json`, `main/schedule/set`/`state`, Phase 15): von vornherein als eigener Bereich reserviert.
- `docs/requirements.md`, `docs/spec/11-sammelbefehle.md`, `docs/spec/14-programme.md`, `docs/spec/15-wochenplan.md` entsprechend angepasst. Reine Doku-/Design-Änderung, noch keine Codeänderung.

### Phase 14 — Bewaesserungsprogramme umgesetzt, Stack-Overflow-Bug gefunden und gefixt

- `ConfigStore`: neue Datei `/programs.json` (eigenes Load/Save/`toJson()`), `kMaxPrograms=8`, `kProgramsJsonCapacity=2048`, `ProgramInput`-Struct + `setPrograms()`/`getProgramCount()`/`getProgramName()`/`programHasTime()`/`getProgramTime()`/`programHasAuto()`/`getProgramAuto()`/`getActiveProgram()`/`setActiveProgram()`/`programsToJson()`.
- `MqttManager`: `main/program/cmd`/`state` (Singular, Index-Auswahl) + `main/programs/set`/`state` (Plural, Bulk-JSON) verdrahtet. `applyProgram()` als Kernfunktion wiederverwendet `applyTimeValue()`/`applyAutoValue()` aus Phase 11 (kein Duplikat). MQTT-/Payload-Puffer auf 2048 Byte angehoben.
- Testet automatisiert per Python/paho-mqtt-Skript gegen den echten Broker (selbst geschrieben, auf Nutzerwunsch als Alternative zu manuellem `mosquitto_pub`) — deckt alle 6 Testfaelle aus der Spec plus einen Bonus-Test fuer die Teilmengen-Semantik ab.
- **Bug gefunden**: `main/programs/set` liess das Board mit `Guru Meditation Error: Core 0 panic'ed (Stack protection fault)` abstuerzen (per Serial-Monitor-Mitschnitt bestaetigt, `Detected in task "loopTask"`, SP unterhalb der Stack-Grenze). Ursache: mehrere 2048-Byte-JSON-Puffer (`StaticJsonDocument`, `char payloadStr[]`) gleichzeitig auf dem Stack der Arduino-`loopTask`, deren Default (8192 Byte) dafuer zu knapp war.
- **Fix**: `SET_LOOP_TASK_STACK_SIZE(16 * 1024)` in `main.cpp` (RAM-Headroom war reichlich vorhanden). Nach Re-Flash liefen alle 14 Testfaelle sauber durch.
- Anbindung der Touch-UI-Buttons `P1`–`P4` (Phase 13) an die ersten vier Programme ist weiterhin ein eigener, noch offener Schritt.

## Offene Punkte / nächste Schritte

- `P1`–`P4`-Anbindung im Touch-UI an die ersten vier Programme (Phase 13/14) — eigener Schritt, noch offen.
- Phase 15 (Zeitplan/Scheduler, Tages- + Wochenplan) ist grob spezifiziert im Backlog, noch nicht priorisiert.
- Phase 10 (Home Assistant MQTT-Discovery) bewusst ans Ende der Bearbeitungsreihenfolge gestellt.
- Vollständiger, zusammenhängender Regressionsdurchlauf aller MQTT-Funktionalitäten (Checkliste in `docs/spec/12-aufraeumen.md`) steht als letzter Schritt vor dem produktiven Einsatz noch aus.
