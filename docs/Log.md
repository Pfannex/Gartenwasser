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

## 2026-08-16

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

### Vollstaendiger MQTT-Regressionstest (autonom, Checkliste aus Phase 12)

- Auf Nutzerwunsch vollstaendig autonom durchgefuehrt (Nutzer war nicht anwesend), inkl. eigenstaendiger Bug-Diagnose (siehe Phase-14-Eintrag oben) und Wiederherstellung des Ausgangszustands am Ende.
- Ein zusammenhaengendes Python/paho-mqtt-Skript deckt 8 der 10 Punkte aus der Checkliste in `docs/spec/12-aufraeumen.md` automatisiert ab: Boot/Verfuegbarkeit, Ventile manuell + V0-Kopplung, Laufzeit (inkl. `maxTime`-Deckelung und einem echten 60s-Zeitablauf mit automatischer Abschaltung), Automatik-Flag, Automatik-Sequenz (inkl. manuellem Vorruecken und Abbruch), Alias (Umlaute, zu lang, Steuerzeichen), Konfiguration per JSON (Teil-Update, unbekannter Key), Persistenz.
- **Persistenztest mit echtem Hardware-Reset**: `esptool --after hard-reset` (RTS-Pin-Puls, ohne Reflash) ausgeloest, danach verifiziert, dass `time`/`auto`/`alias` und die Programme-Liste den Neustart ueberleben, alle Ventile AUS sind und keine Automatik-Sequenz automatisch weiterlaeuft.
- **Ergebnis: 48 von 49 Checks bestanden.** Die eine „fehlgeschlagene" Pruefung war ein Timing-Artefakt im Testskript (Restlaufzeit `00:59` statt exakt `01:00` gelesen, da zwischen Einschalten und Pruefung bereits ein Sekunden-Tick lief) — kein Firmware-Fehler, die `maxTime`-Deckelung selbst war korrekt.
- **Nicht automatisiert** (physischer Eingriff noetig, bewusst nicht unbeaufsichtigt ausgefuehrt): Diagnostics-Fehlerfall (I2C-Kabel ziehen, Punkt 6) und Verbindungsabbruch-Resilienz (WLAN/MQTT trennen, Punkt 9) — diese zwei wurden im Anschluss interaktiv nachgeholt, siehe naechster Eintrag.
- Vor/nach dem Testlauf wurde der komplette Konfigurationsstand (`config`, `programs`) gesichert und am Ende per `main/config/set`/`main/programs/set` wiederhergestellt — keine bleibenden Aenderungen am Geraet.

### Manuelle Regressionstest-Punkte nachgeholt (I2C-Fehlerfall, WLAN-Resilienz)

- Interaktiv durchgefuehrt: Nutzer fuehrt die physische Aktion aus (I2C-Kabel ziehen, WLAN am Router trennen), waehrenddessen wertet ein Live-MQTT-Mitschnitt (Python/paho-mqtt) das Ergebnis aus.
- **Eigener Fehler gefunden und behoben unterwegs**: der erste Versuch fuer Punkt 9 scheiterte, weil versehentlich mehrere Kopien des Monitor-Skripts mit identischer MQTT-Client-ID gleichzeitig liefen (ein fehlgeschlagener Hintergrund-Start war nicht sauber beendet worden) — die haben sich gegenseitig vom Broker geworfen (Reconnect alle 1-2s). Ursache erkannt, verwaiste Prozesse beendet, Monitor-Skript mit eindeutiger Client-ID neu geschrieben, Test sauber wiederholt.
- **Punkt 6 (I2C-Fehlerfall)**: I2C-Bus kurz getrennt → `diagnostics/i2cStatus` wechselte auf `error`, `lastError` wurde gesetzt (`"MCP23017 nicht erreichbar (I2C-Bus-Ausfall)."`), nach Wiederverbinden (13s spaeter) zurueck auf `ok`. PASS.
- **Punkt 9 (Resilienz)**: Automatik-Sequenz gestartet (V1 3 Min → V2 2 Min), waehrend V1 lief WLAN am Router fuer ~49s getrennt. Board meldete den Verbindungsverlust selbst in `lastError`. Nach Reconnect zeigte `V1/time/remaining` exakt den rechnerisch korrekten Wert (Restzeit minus real vergangene Zeit) — die lokale Restlaufzeit war waehrend des Ausfalls **nicht** pausiert oder zurueckgesetzt worden. Der Uebergang V1→V2 erfolgte anschliessend auf die Sekunde genau zur erwarteten Zeit. Bestaetigt die Kernanforderung "laeuft lokal/autonom weiter" empirisch mit Zeitstempeln. PASS.
- Damit sind jetzt alle 10 Punkte der Checkliste aus `docs/spec/12-aufraeumen.md` abgedeckt und bestanden. Details/Zeitstempel siehe `docs/testing.md`.

### Log-Meldungen WLAN/MQTT-Verbindungsverlust unterscheidbar gemacht

- Beide loggten bisher identisch "Verbindung verloren." — in `diagnostics/lastError` (kein Source-Praefix wie im Serial-Log) war dadurch nicht erkennbar, welche Verbindung gemeint war (aufgefallen beim Auswerten des Resilienztests oben).
- Jetzt "WLAN Verbindung verloren." (`WifiManager.cpp`) bzw. "MQTT Verbindung verloren." (`MqttManager.cpp`). Geflasht und gepusht.

### Design: `shortcut`-Feld fuer Programme (Vorbereitung `P1`-`P4`-Anbindung)

- Auf Nutzervorschlag ergaenzt, bevor die eigentliche Touch-UI-Anbindung umgesetzt wird: `programs.json` bekommt ein optionales `shortcut`-Feld je Programm (`"P1"`-`"P4"`), das die Button-Bindung von der Array-Position entkoppelt — sonst wuerde ein Umsortieren via `main/programs/set` (Array-Replace) die Belegung stillschweigend verschieben.
- Duplikate werden bewusst **nicht** beim Schreiben abgelehnt (widerspraeche dem Array-Replace-Prinzip), sondern "erster Treffer in Array-Reihenfolge gewinnt" beim Aufloesen, zusaetzlich ein nicht-blockierender Log-Hinweis bei erkannten Duplikaten. Mit dem Array-Replace ist das sogar einfacher als bei inkrementellen Updates: ein einziger linearer Scan ueber max. 8 Eintraege reicht, weil `main/programs/set` immer die komplette finale Liste liefert.
- Intern als `uint8_t` (0/1-4) gespeichert, analog zu `activeProgram`. Details in `docs/spec/14-programme.md` (Kernentscheidung 8) und `docs/requirements.md` (Entscheidungshistorie).

### `P1`-`P4`-Anbindung umgesetzt, Grundsatzentscheidung "Automatik erfordert Programm"

- `ConfigStore`: `shortcut`-Feld implementiert (Laden/Speichern/JSON, `getProgramIndexForShortcut()`). Statische String-Literale (`"P1"`.."P4"") statt lokaler Puffer bei der JSON-Ausgabe verwendet, um kein Dangling-Pointer-Risiko einzugehen.
- `MqttManager`: `requestProgramByShortcut()` (wendet an) und `requestProgramClear()` (waehlt ab, wie `main/program/cmd 0`) neu, oeffentlich fuer die Touch-UI.
- `HmiManager`: `P1`-`P4`-Buttons wenden das gebundene Programm an; erneuter Druck auf ein bereits aktives Programm waehlt es wieder ab (Toggle). Ohne Bindung: transienter Hinweis "P{n} nicht konfiguriert!" (2s, orange). Checked-Status der Buttons wird periodisch aus `ConfigStore::getActiveProgram()` abgeleitet statt aus dem lokalen Klick-Zustand, bleibt so auch bei MQTT-Aenderungen korrekt.
- **Bug gefunden und gefixt**: Duplikat-Log-Meldung fuer Shortcuts wurde im 96-Byte-`lastError`-Puffer abgeschnitten (`ConfigStore`, `Diagnostics::lastError`). Auf Wunsch des Nutzers auf `"Shortcut Px doppelt belegt!"` gekuerzt.
- **Design-Diskussion waehrend des Hardware-Tests**: urspruenglich sollte `main/cmd ON` ohne gewaehltes Programm nur einen Hinweis zeigen, aber trotzdem starten (nutzt dann die aktuellen `auto`-Flags, wie seit Phase 7). Nutzer-Feedback nach dem ersten Test ("es laeuft auch V1 los, das darf dann nicht sein") fuehrte zu einer Grundsatzentscheidung: **eine Automatik ohne gewaehltes Programm ergibt keinen Sinn mehr**, seit es Programme gibt. `startSequence()` (gemeinsam von Touch und MQTT genutzt) bricht jetzt ab, wenn `activeProgram == 0` - betrifft `main/cmd ON` ueberall (auch spaetere Home-Assistant-Automatisierungen), **nicht** aber direktes Ventilschalten (`V{n}/cmd`), das weiterhin uneingeschraenkt manuell funktioniert.
- Ist ein Programm gewaehlt, wendet `startSequence()` es beim Start zusaetzlich nochmal frisch an (`applyProgram()`), damit garantiert dessen Werte laufen statt zwischenzeitlich manuell abgewichener Flags.
- Statuszeile: "Bereit" durch "MANUELL" ersetzt, wenn kein Programm gewaehlt ist (Nutzer-Begruendung: "Bereit ohne Programmanwahl macht keinen Sinn, da dann beim Druecken auf AUTO ja auch nichts passiert, wie kann das dann bereit sein?"). "MANUELL" bleibt bewusst neutral/grau (kein Fehler, sondern ein normaler, dauerhafter Betriebsmodus fuer direktes Ventilschalten).
- Getestet: interaktiv auf Hardware (Nutzer bedient Display, Ergebnis per gezielten MQTT-Abfragen/Live-Mitschnitten gegengeprueft) - alle Faelle bestanden, siehe `docs/testing.md`.
- Nebenbei: eigener Fehler beim ersten Beobachtungsversuch gefunden (zwei Monitor-Prozesse mit gleicher Client-ID liefen gleichzeitig, siehe Eintrag oben) sowie eine Arbeitsweise-Korrektur vom Nutzer uebernommen: vor Tests mit noetiger Nutzeraktion immer zuerst ankuendigen und auf "ok" warten, statt sofort Vorbedingungen zu setzen und ein Zeitfenster zu starten (jetzt in Claude-Memory hinterlegt).

### Phase 15 — erster Design-Entwurf fuer den Zeitplan

- JSON-Schema-Vorschlag: `schedule.json`/`main/schedule/set`/`state` (Array-Replace wie bei `programs`), je Eintrag `type` (`daily`/`weekly`/`once`) mit typ-spezifischen Feldern (`weekdays`/`date`), `time`, `program`-Referenz, `enabled`.
- **Programm-Referenz per Name statt Array-Index** (Nutzerentscheidung) — vermeidet das Drift-Problem, das die `shortcut`-Felder bei den Programmen bereits geloest haben: ein Umsortieren via `main/programs/set` wuerde sonst Zeitplan-Eintraege stillschweigend auf ein anderes Programm verschieben.
- **Verpasste Trigger verfallen** (Nutzerentscheidung, mit Begruendung): ein nachgeholter Trigger zu unvorhersehbarer Zeit (z. B. erst beim naechsten Boot Stunden spaeter) waere ueberraschender als ein einmalig ausgefallener Termin.
- **Globaler Ein/Aus-Schalter** fuer den kompletten Zeitplan gewuenscht: `enabled`-Feld auf oberster Ebene + Convenience-Topic `main/schedule/cmd` (`ON`/`OFF`), analog zu `main/program/cmd`.
- **Zwei neue Nutzer-Merker**, noch nicht verfeinert:
  - Kollisions-Hinweis bei manuellem `main/cmd ON` (Touch oder MQTT), wenn `main/remainingTotal` voraussichtlich ueber den naechsten faelligen Zeitplan-Trigger hinausreicht — nicht blockierend, nur Information.
  - Aufraeum-Funktion (Arbeitstitel `main/schedule/cleanup`) zum gezielten Entfernen abgelaufener `once`-Eintraege auf Anfrage, ergaenzt aber ersetzt nicht das "liegen lassen"-Standardverhalten.
- Rein Design-/Dokumentationsstand, noch keine Codeaenderung. Details in `docs/spec/15-wochenplan.md`, `docs/requirements.md` (Entscheidungshistorie).

### Phase 15 — Feldreferenz fuer den Zeitplan verfeinert

- Kurz diskutiert, ob `name` gleichzeitig die Programm-Referenz sein koennte (spart ein Feld) - verworfen: dann koennte ein Eintrag nicht mehr eindeutig sein UND trotzdem zweimal dasselbe Programm referenzieren (z. B. "Rasen" taeglich + zusaetzlich dienstags).
- Ebenso verworfen: ein zusaetzliches eindeutiges `index`-Feld je Eintrag - anders als bei Programmen (P1-P4-Buttons, `main/program/cmd` brauchen eine stabile Referenz von aussen) zeigt beim Zeitplan nichts von aussen auf einen einzelnen Eintrag, `main/schedule/set` ersetzt immer die komplette Liste als Block.
- Ergebnis: eigenes `program`-Feld (Name-Referenz, darf sich ueber mehrere Eintraege wiederholen, keine Eindeutigkeitspflicht) bleibt bestehen, `name` ist optional und rein kosmetisch.
- Vollstaendige Feldreferenz-Tabelle (Ebene/Pflicht/Werte/Bedeutung je Key) sowie 5 zusaetzliche Beispiele (Mehrfachnutzung eines Programms, pausierter Einzeleintrag, minimaler Eintrag ohne `name`, komplett pausierter Zeitplan, mehrere Wochentage in einem Eintrag) in `docs/spec/15-wochenplan.md` ergaenzt.
- Rein Design-/Dokumentationsstand, noch keine Codeaenderung.

### Phase 15 — Scheduler-Mechanik erlaeutert (Merker 2 aufgeloest)

- Mechanik festgelegt: minuetlicher Prueflauf ueber die komplette `schedule`-Liste (kein Timer pro Eintrag), analog zum bestehenden sekuendlichen Ventil-Tick in `MqttManager::loop()` - `lastCheckedMinute` merken, bei Minutenwechsel einmal die ganze Liste durchgehen, dadurch garantiert genau einmal pro Minute ausgewertet.
- Match-Logik pro Eintrag (daily/weekly/once) faehrt beim Treffer denselben Startpfad wie `main/cmd ON` (`startSequence()`, inkl. Programm-Pflicht + Reapply aus Phase 14).
- Nebenbei die "gleichzeitige Trigger"-Frage aufgeloest: `startSequence()` prueft schon `Sequencer::isRunning()`, daher gewinnt bei zwei Treffern in derselben Minute automatisch der erste in Array-Reihenfolge, kein neuer Code noetig.
- Klar abgegrenzt von einer zweiten, separaten Funktion "naechster faelliger Trigger" (Wiederkehr-Mathematik fuer daily/weekly) - die wird fuer den Kollisions-Hinweis (Merker 1) noch gebraucht, ist aber nicht Teil des einfachen Minuten-Checks.
- Details in `docs/spec/15-wochenplan.md`, Kernentscheidung 4. Rein Design-Klaerung, noch keine Codeaenderung.

### Phase 15 — Merker 1 (settingsError-Topic) verworfen

- Da ueberschneidende Zeitplan-Eintraege bewusst erlaubt sind und ueber "erster in Listen-Reihenfolge gewinnt" automatisch aufgeloest werden (siehe Eintrag oben), gibt es dafuer keinen Fehlerfall zu melden - das eigens angedachte Topic `main/schedule/settingsError` wird nicht gebraucht.
- Allgemeine Konfigurationsfehler (z. B. ungueltiger `program`-Name) laufen stattdessen ueber den bereits bestehenden generischen Kanal `diagnostics/lastError`, wie ueberall sonst im Projekt.
- Details in `docs/spec/15-wochenplan.md`, Abschnitt "Verworfen".

### Phase 15 — Zeitplan/Scheduler umgesetzt und getestet

- `ConfigStore`: neue Datei `/schedule.json`, `kMaxScheduleEntries=16`, `kScheduleJsonCapacity=4096`. `ScheduleType`-Enum (DAILY/WEEKLY/ONCE), `ScheduleInput`-Struct + `setSchedule()` (Bulk-Replace analog `setPrograms()`), Getter je Eintrag (0-basiert, kein von aussen ansprechbarer Identifikator noetig - main/schedule/set ersetzt immer die komplette Liste). Neue `getProgramIndexForName()` fuer die Programm-Referenz per Name. Zeit/Datum-Strings ("HH:MM"/"YYYY-MM-DD") werden beim Serialisieren ueber Arduino `String` statt lokaler `char`-Puffer ausgegeben, um denselben Dangling-Pointer-Fehler wie bei den `shortcut`-Strings (Phase 14) von vornherein zu vermeiden.
- `MqttManager`: `main/schedule/set`/`state` (Bulk), `main/schedule/cmd` (globaler ON/OFF-Schalter, Convenience analog `main/program/cmd`), `main/schedule/cleanup` (entfernt abgelaufene "once"-Eintraege). Ungueltige Einzeleintraege werden uebersprungen + geloggt, nicht die ganze Anfrage abgelehnt.
- **Scheduler-Tick** (`checkSchedule()`): minuetlicher Pruefloop ueber die komplette Liste, laeuft unconditional in `MqttManager::loop()` neben Ventil-Tick/Diagnostics (unabhaengig von WLAN/MQTT). Deaktiviert sich selbst ohne verlaessliche Echtzeit (neue `Logger::isRealTimeEnabled()`-Abfrage). Bei Match: `applyProgram()` (per Name aufgeloest) + `startSequence()` - derselbe Pfad wie `main/cmd ON`.
- **Stack-Vorsorge**: `kScheduleJsonCapacity` (4096 Byte) ist jetzt der puffer-bestimmende Fall (groesser als `kProgramsJsonCapacity`). `SET_LOOP_TASK_STACK_SIZE` in `main.cpp` von 16 KB auf 32 KB verdoppelt, um denselben Stack-Overflow-Bug wie bei Phase 14 von vornherein zu vermeiden (RAM-Headroom reichlich vorhanden).
- **Getestet** (automatisiert per Python/paho-mqtt, ad-hoc-Skript): Bulk-Set aller drei Trigger-Typen, globaler Schalter, Einzelvalidierung (5 gemischte Eintraege, nur der gueltige bleibt), Cleanup-Funktion (nur abgelaufene `once`-Eintraege entfernt), und ein **echter Zeit-Test**: Trigger 2 Minuten in der Zukunft gesetzt, ~130s gewartet, feuerte exakt zur Minute (Programm automatisch angewendet, Sequenz gestartet). Alle Faelle bestanden, siehe `docs/testing.md`.
- Bewusst zurueckgestellt, nicht Teil dieser Umsetzung: Kollisions-Hinweis bei manuellem `main/cmd ON` nahe am naechsten Trigger (braucht eine separate "naechster faelliger Trigger"-Berechnung mit Wiederkehr-Mathematik), sowie jede Touch-UI-Anbindung fuer den Zeitplan (siehe Backlog-Idee in `docs/spec/13-touch-ui.md`).

### Phase 15 — `name`-Feld wieder entfernt

- Auf Nutzerwunsch nach der ersten Umsetzung: das anfangs vorgesehene, rein kosmetische `name`-Feld je Zeitplan-Eintrag war ueberfluessig - "alles basiert auf dem Key `program`". `program` bleibt der einzige Identifikator eines Eintrags.
- Betroffen: `ConfigStore` (`ScheduleInput`/`StoredScheduleEntry` ohne `name`, `getScheduleName()` entfernt), `MqttManager` (`handleScheduleSet()`/`handleScheduleCleanup()` ohne `name`-Handling).
- Auf Hardware verifiziert: Eintraege ohne `name` funktionieren unveraendert korrekt (Bulk-Set/State-Roundtrip getestet).
- Details in `docs/spec/15-wochenplan.md`, Abschnitt "Nachtrag".

## 2026-08-17

### Touch-UI komplett neu gestaltet, `P1`–`P4`/`shortcut` entfernt

- Mehrstufiger Design-Dialog vor der Umsetzung: Programm-Anzahl-Limit (nur 4 `P1`–`P4`-Buttons von bis zu 8 möglichen Programmen) hinterfragt → `ConfigStore::kMaxPrograms` 8→32 angehoben. Idee einer 2×8-Ventilanzeige (volle MCP23017-Kapazität) aufgekommen, dabei ein konkretes technisches Risiko identifiziert (`MqttManager::parseValveTopic()` nimmt aktuell einstellige Ventilnummern an, würde bei `V10`–`V15` brechen) — Entscheidung: Erweiterung auf 16 Ventile als eigene, separate, noch nicht begonnene Phase, nicht Teil dieses Umbaus, genau wie ein geplantes Web-Interface zur vollständigen Geräte-Konfiguration (Touch-UI bleibt bewusst nur für schnelle Vor-Ort-Bedienung zuständig, Web-UI wird die eigentliche Editier-Oberfläche).
- Ursprünglich angedachte „Timer“-Unterseite (Zeitplan-Bedienung am Display, siehe Backlog-Idee vom 2026-08-16) verworfen — kein sinnvoller Kalender auf 172×320px, und Zeitplan-Einträge haben seit dem Entfernen des `name`-Felds ohnehin keinen sprechenden Titel mehr zum Durchblättern.
- Eigene Korrektur während des Designs: ich hatte vorgeschlagen, dass OK in der Programme-Unterseite das Programm sowohl auswählt **als auch startet** (kombiniert). Nutzer korrigierte das explizit: „das soll nicht so sein, erst Programm auswählen, dann von der Hauptseite mit START starten“ — OK wendet seitdem nur an, Start bleibt ein bewusst separater Schritt.
- **Umsetzung**:
  - `ConfigStore`: `kMaxPrograms` 8→32, `kProgramsJsonCapacity` 2048→8192. `shortcut`-Feld (Struct, JSON-Serialisierung/-Parsing, Duplikat-Prüfung, `getProgramIndexForShortcut()`) komplett entfernt statt nur ungenutzt gelassen — sein einziger Zweck (P1-P4-Bindung) entfiel mit den Buttons.
  - `MqttManager`: `requestProgramByShortcut()`/`requestProgramClear()` durch ein generisches `requestProgramSelect(uint8_t programIndex)` ersetzt (0 = abwählen). Neue `requestValveCmd(uint8_t, bool)` — `handleValveCmd()`s Kernlogik nach `applyValveCmd()` extrahiert, für die jetzt funktionale Ventilmatrix ohne MQTT-Umweg wiederverwendet.
  - `main.cpp`: `SET_LOOP_TASK_STACK_SIZE` 32→64 KB (das gewachsene `kProgramsJsonCapacity` ist jetzt der puffer-bestimmende Fall, vorher `schedule.json`).
  - `HmiManager`: komplette Neugestaltung. Hauptseite: graue statische Titelzeile, START/STOP-Button (voller Breite, reduzierte Höhe), 4×4-Ventil-Statusmatrix (`V0`–`V5` belegt, Rest Platzhalter für die spätere 16-Ventil-Option, `V1`–`V5` per Tap direkt schaltbar über `requestValveCmd()`, `V0` ohne Handler wie bei MQTT), Programme-Button darunter (zeigt aktives Programm als Buttontext), zweizeilige Statuszeile als Fußleiste (Fehler rot/gelb > Programm-Hinweis orange > laufende Sequenz gelb > „MANUELL“ hellblau bei manuellem Ventil, jeweils mit Alias-Name in Zeile 2). Neue Programme-Unterseite als zweiter LVGL-Screen (`lv_scr_load()`, architektonisch neu für dieses Projekt) mit `<`/`>`-Blättern durch alle Programme inkl. virtuellem „Kein Programm“-Eintrag, OK/Abbrechen.
- **Iteratives Feintuning direkt am Gerät** (viele Build-Flash-Test-Runden): gelber Rahmen ums aktive Ventil entfernt (Rot allein reicht), Statuszeilen-Hintergrund an die Titelzeilen-Farbe angeglichen, Fehleranzeige auf rot/gelb umgestellt, Button-/Box-Höhen und Textposition mehrfach nachjustiert (START/Programme-Button aktuell gleich hoch, Statuszeilen-Text bewusst mit negativem Offset weiter oben positioniert). Nutzer-Fazit: funktional fertig, Feinschliff der Abstände „vielleicht nochmal später hübsch machen“ — bewusst als kosmetisch offen gelassen, kein Blocker.
- Getestet interaktiv auf Hardware (Build→Flash→Sichtprüfung je Änderung), siehe `docs/testing.md`. Dokumentation nachgezogen: `docs/spec/13-touch-ui.md` (Nachtrag), `docs/requirements.md` (Touch-UI-Abschnitt + Entscheidungshistorie + Programme-Beispiel ohne `shortcut`), `docs/testing.md`.

### Erster Hardware-Test der Programme-Unterseite — vier Nachjustierungen

- 5 Testprogramme per MQTT gesetzt (je ein Ventil auf `auto`+1 Minute) und am Gerät durchgeblättert — funktioniert. Dabei aufgefallen: das zuvor gesetzte `activeProgram` zeigte auf einen inzwischen ungültigen Index (7, aus einem älteren 8-Programme-Test) und musste einmalig manuell zurückgesetzt werden (`main/program/cmd 0`).
- Buttontext bei keinem gewählten Programm von „Kein Programm gewählt“ auf „Kein Programm“ gekürzt (passte nicht in den Button).
- **Bug gefunden und gefixt**: die 5 Testprogramme wurden zunächst so gesetzt, dass jedes nur sein *eigenes* Ventil im `auto`-Feld nennt (Teilmengen-Semantik) — beim Anwenden blieben die `auto`-Flags der anderen Ventile dadurch auf ihrem alten Stand (von früheren Tests noch `true`) stehen, sichtbar an der Touch-UI-Matrix („da sind alle auf auto“). Für dieses Testszenario auf explizite Vollangabe aller 5 Ventile je Programm umgestellt (nur das eigene `true`). Zusätzlich, als generelle Verhaltensänderung: `MqttManager::applyProgram(0)` („Kein Programm“ wählen) setzt jetzt selbst alle `auto`-Flags explizit auf `false` zurück, statt sie unangetastet zu lassen — sonst bliebe der `auto`-Zustand des zuletzt aktiven Programms hängen, obwohl die Anzeige „Kein Programm“ zeigt. Gilt für Touch **und** MQTT (`main/program/cmd 0`).
- Programme-Button während laufender Automatik-Sequenz gesperrt (`LV_STATE_DISABLED`, nicht klickbar) — ein Programmwechsel mitten im Lauf hätte sonst zu inkonsistentem Zustand geführt ("das gibt sonst Murks", Nutzer-Zitat).
- Alle Touch-Buttons (START, Ventilmatrix, Programme, `<`/`>`/OK/Abbrechen) zeigen jetzt beim Antippen kurz Weiß als Trefferfeedback (`addPressHighlight()`, neuer Helfer in `HmiManager.cpp`) — vorher keine sichtbare Rückmeldung, ob ein Touch registriert wurde.
- Auf Hardware getestet und vom Nutzer bestätigt („schaut gut aus, erledigt“). Details siehe `docs/spec/13-touch-ui.md`, Nachtrag, und `docs/testing.md`.

### Zeitplan (Phase 15) nach dem HMI-Umbau erneut auf Hardware verifiziert

- Merker vom 2026-08-16 aufgelöst: `once`-Eintrag 5 Minuten in der Zukunft (`18:00`) gesetzt, referenziert Programm „V1" (eines der 5 an diesem Tag geladenen Testprogramme). Trigger feuerte pünktlich, Programm korrekt angewendet, Sequenz lief die konfigurierte Minute durch und beendete sich selbstständig — vom Nutzer live am Display verfolgt und bestätigt („sauber angelaufen, das können wir abhaken!"). Bestätigt, dass der Scheduler nach den `shortcut`-Entfernung/`kMaxPrograms`-Änderungen weiterhin korrekt mit dem Programm-Bestand zusammenspielt. Details siehe `docs/testing.md`.

### Kollisions-Hinweis (Phase 15) endgültig verworfen

- Der seit 2026-08-16 offene Merker „Kollisions-Hinweis bei manuellem `main/cmd ON` nahe am nächsten Zeitplan-Trigger" wurde auf Nutzerentscheidung komplett gestrichen, nicht nur zurückgestellt — Begründung: die dafür nötige „nächster fälliger Trigger"-Berechnung (Wiederkehr-Mathematik für `daily`/`weekly`) wäre unverhältnismäßig aufwendig für den gebotenen Nutzen (Nutzer-Zitat: „der Nutzen ist auch mäßig").
- Der bereits vorhandene `Sequencer::isRunning()`-Guard in `startSequence()` (siehe Phase-15-Kernentscheidung 4, „gleichzeitige Trigger") deckt die eigentliche Sicherheitsanforderung ohnehin schon vollständig ab — nicht nur für zwei gleichzeitig fällige Zeitplan-Einträge, sondern genauso für einen manuellen Start waehrend eines geplanten Laufs und umgekehrt: „wer zuerst kommt, malt zuerst" (Nutzer-Bestätigung), der zweite Versuch wird abgewiesen und geloggt (`"main/cmd ON ignoriert (Automatik laeuft bereits)."`).
- Details/Aktualisierung in `docs/spec/15-wochenplan.md` (Abschnitt „Verworfen", Status-Zeile bereinigt) und `docs/requirements.md` (Entscheidungshistorie).

### Touch-UI-Zeitplanbedienung endgültig verworfen

- Nutzerentscheidung: das 172×320px-Display ist für eine Zeitplan-Bedienung zu klein — keine „Timer"-Unterseite, auch nicht in reduzierter Form. Zeitplan-Bearbeitung bleibt vollständig dem geplanten Web-Interface vorbehalten. Details siehe `docs/spec/13-touch-ui.md`, Abschnitt „Endgültig verworfen".
- Damit sind alle Backlog-Punkte der ursprünglichen Touch-UI-Untermenü-Idee (siehe Eintrag vom 2026-08-16) entweder umgesetzt (Programme-Unterseite) oder bewusst verworfen (Timer-Unterseite).

### Web-Interface geplant: sechs Phasen (16–21), vor Phase 10 eingeordnet

- Ressourcen-Check vorab: `app0`-Partition (3 MB) aktuell 41,5 % belegt, `spiffs`-Partition (1,875 MB) fast leer, RAM 34,6 % belegt. Partitionstabelle (`partitions.csv`) hat bereits eine vollständige Dual-OTA-Auslegung (`app0`/`app1`, `otadata`) — keine Änderung nötig für ein späteres Firmware-Update über die Weboberfläche. Grobschätzung für das komplette Web-Interface: ≈1,7 MB von 3 MB (≈56 %), reichlich Marge. Auf Nutzerfrage („sollten wir die Voraussetzungen für OTA zuerst angehen, um die Ressourcen im Griff zu behalten?") geantwortet: nicht nötig, die eigentliche Voraussetzung (Dual-Partition) ist bereits erfüllt; stattdessen Größen-Checkpoint direkt nach Phase 16 vorgesehen (dort der erwartete größte Sprung).
- Backend-Empfehlung: `ESPAsyncWebServer`+`AsyncTCP` (De-facto-Standard für nicht-blockierende ESP32-Webserver, passt zum durchgehaltenen Non-Blocking-Prinzip des Projekts), `LittleFS` statt `SPIFFS` (moderner Nachfolger, besser für mehrere benannte Dateien geeignet). Für Phase 21 (OTA) `ElegantOTA` statt `Update.h` von Hand vorgeschlagen.
- Zentrale, noch offene Architekturfrage für Phase 16: hostet der ESP32 eine eigene REST/WebSocket-API (A), oder liefert er nur statische Dateien aus und der Browser spricht per MQTT-over-WebSocket direkt mit dem Broker (B, würde aber einen WebSocket-Listener auf dem Mosquitto-Broker voraussetzen)? Tendenz aus der Diskussion Richtung B (passt zur stark MQTT-zentrierten Architektur des Projekts), noch nicht final entschieden — wird im angekündigten „Interview"-Verfahren mit dem Nutzer geklärt.
- Drei visuelle Stilrichtungen mit identischen Bedienelementen (Ventilstatus, Automatik-Schalter, Laufzeit, Programm-Karte, Zeitplan-Zeile, Buttons) als Artefakt vorgelegt: „Native Minimal", „Dashboard Cards", „Control Panel". Nutzer wählt **„Dashboard Cards"** (farbige Statuschips, weiche Karten, nah an Home-Assistant-Formsprache) — passt tendenziell zu Alpine.js/htmx als Frontend-Ansatz (noch nicht final entschieden, siehe Architekturfrage).
- Sechs Phasen angelegt (`docs/spec/16-webif-fundament.md` bis `docs/spec/21-webif-ota.md`), vom einfachsten zum komplexesten Datenmodell und OTA bewusst zuletzt: 16 Fundament & Architekturentscheidung, 17 Status-Dashboard (read-only), 18 Konfiguration, 19 Programme, 20 Zeitplan, 21 Firmware-Update. `docs/README.md` (Phasen-Übersicht) und `docs/requirements.md` (Entscheidungshistorie) entsprechend ergänzt. Noch keine Codeänderung — reine Planungsphase.

### Phase 16 (Web-Interface-Fundament) umgesetzt und getestet

- Interview-Ergebnis: **Architektur B** entschieden (Browser spricht per MQTT-over-WebSocket direkt mit dem Broker statt einer eigenen ESP32-API) — Nutzer fragte dabei selbst, ob jedes Gerät, das die Webseite öffnet, auch den Broker erreicht; Antwort: ja im Normalfall (gleiches Subnetz wie Broker), Zugriff von unterwegs bräuchte dieselbe Netzwerkroute (VPN) wie der ESP32 selbst — als Hinweis in der Spec festgehalten, kein Blocker. **Alpine.js** als Frontend (kein Build-Schritt) und **`LittleFS`** statt `SPIFFS` ebenfalls im Interview bestätigt.
- Umsetzung: `ESPAsyncWebServer`+`AsyncTCP` (`ESP32Async`-Fork) neu eingebunden, `ConfigStore` komplett von `SPIFFS.h` auf `LittleFS.h` umgestellt (11 Fundstellen), neue Klasse `WebManager` (reines File-Serving, kein `loop()` nötig - vollständig async). Neue `Logger::Source::WEB` ergänzt (Nutzerwunsch), eigene Log-Kategorie `WEB  ` analog zu `WIFI `/`MQTT `/`I2C  `/`HMI  `.
- **Aufräumarbeit nebenbei**: mehrere Code-Kommentare aus dem Touch-UI-Umbau trugen faelschlich das Label "Phase 16" (informelle Bezeichnung waehrend der Umsetzung, bevor die echte Phase 16 vergeben wurde) - alle sieben Fundstellen (`MqttManager.h/.cpp`, `ConfigStore.h`, `HmiManager.cpp`, `main.cpp`) auf "Touch-UI-Neugestaltung" umbenannt, um Verwechslung mit dem jetzt tatsaechlichen Phase-16-Web-Interface zu vermeiden.
- **Bug gefunden**: `pio run --target uploadfs` (`mklittlefs`) erzeugt ein LittleFS-Image, das der arduino-esp32-3.x-Laufzeit-Mount beim ersten Boot nicht als gueltig erkennt und automatisch neu formatiert - die vorab hochgeladenen Dateien (`data/index.html`, `data/style.css`) waren danach weg. Per Diagnose isoliert: Laufzeit-Schreiben/Lesen ueber LittleFS funktioniert nachweislich zuverlaessig (Alias-Wert per MQTT gesetzt, ueberlebt Hardware-Reset), nur die offline per `mklittlefs` geschriebenen Dateien sind betroffen.
- **Nebenbefund, unangekuendigt**: die SPIFFS→LittleFS-Umformatierung (ausgeloest durch den ersten `uploadfs`-Lauf) hat die zu diesem Zeitpunkt gesetzten Testdaten (5 Programme "V1"-"V5", 1 Zeitplan-Eintrag, Config-Werte) geloescht. Vom Nutzer als unkritisch bestaetigt (reine Testdaten dieser Session), aber im Nachhinein haette ich das vorher ausdruecklich ankuendigen sollen statt es einfach zu flashen - als Lehre fuer kuenftige Dateisystem-/Partitions-Aenderungen festgehalten.
- Flash-Checkpoint: 41,5 % → 43,1 % (≈+45 KB durch die neuen Bibliotheken), deutlich unter der vorab geschaetzten Groessenordnung.

### Uploadfs/LittleFS-Bug richtig geloest statt nur umgangen

- Zunaechst ein PROGMEM-Workaround umgesetzt (Web-Dateien als C++-Strings in die Firmware eingebettet, beim ersten Boot selbst nach LittleFS geschrieben) - funktionierte, wurde aber als nicht skalierbar erkannt (Alpine.js/`mqtt.js` ab Phase 17 deutlich groesser).
- Stattdessen den eigentlichen Bug isoliert: `ConfigStore::begin()` rief `LittleFS.begin(true)` auf (Auto-Format bei Mount-Problemen) - genau dieses Auto-Format war der Datenvernichter, nicht ein grundsaetzliches Formatproblem. Gezielter Test mit `LittleFS.begin(false)` (kein Auto-Format) auf einem frisch per `uploadfs` beschriebenen Image: Mount erfolgreich, alle Dateien sofort sichtbar, kein Reformat.
- **Fix**: `ConfigStore::begin()` nutzt jetzt dauerhaft `LittleFS.begin(false)`. PROGMEM-Workaround in `WebManager.cpp` wieder vollstaendig entfernt (zurueck auf reines `serveStatic()`), `data/` ist die alleinige Quelle fuer die Web-Dateien.
- Persistenz UND `uploadfs`-Dateien mit dem finalen Fix erneut end-to-end verifiziert (Alias-Wert ueberlebt Reboot; Web-Dateien nach frischem `uploadfs`+Reboot sofort korrekt ausgeliefert). Kehrseite des Fixes: eine wirklich leere, nie per `uploadfs` beschriebene Partition muss einmalig vorbereitet werden, sonst schlaegt der Mount fehl - fuer dieses eine, bereits mehrfach geflashte Geraet unkritisch.
- Details siehe `docs/spec/16-webif-fundament.md`, `docs/testing.md`.

### Phase 17 (Status-Dashboard) umgesetzt und getestet

- WebSocket-Voraussetzung aus Phase 16 erledigt: Nutzer hat auf dem Mosquitto-Broker (2.0.21, Debian-LXC unter Proxmox, `include_dir`-Layout) eine neue `/etc/mosquitto/conf.d/websockets.conf` angelegt (`listener 9001`, `protocol websockets`, `allow_anonymous true` — passend zum bereits anonymen 1883-Listener) und den Dienst neu geladen, per `ss -tlnp | grep 9001` verifiziert.
- `mqtt.min.js` (MQTT.js, 369 KB) und `alpine.min.js` (Alpine.js 3.x, 47 KB) per `curl` von `unpkg.com` bezogen, lokal in `data/` abgelegt (kein CDN zur Laufzeit) - zusammen ≈416 KB, komfortabel in der 1,875-MB-`LittleFS`-Partition.
- Neue `data/app.js`: Alpine-Komponente `dashboard()` verbindet sich per `mqtt.connect("ws://192.168.1.123:9001/mqtt")` direkt mit dem Broker (feste Adresse wie in `tools/mqtt-tests/*.py`, liegt in einem anderen Netzsegment als die Geraete-IP), abonniert `gartenwasser/#`, pflegt reaktiven State fuer Ventile/Sequenz/Programm/Diagnostics. `valveState()` repliziert exakt die Farblogik der Touch-UI-Ventilmatrix (`HmiManager::refreshValveStatus()`).
- `data/index.html` neu aufgebaut (Kopfzeile mit Verbindungs-/Online-Status, 2x3-Ventilkachel-Grid, Sequenz-/Programm-Karte, bedingte Fehler-Karte), `data/style.css` um `.header-bar`/`.valve-grid`/`.valve-tile`/`.valve-alias` ergaenzt. Keine Firmware-Aenderung noetig - `WebManager`s bestehendes `serveStatic()` reicht aus.
- Vor dem Browser-Test die komplette Pipeline ohne Browser verifiziert: `paho-mqtt` mit `transport="websockets"` gegen den neuen Listener verbunden (simuliert exakt, was `mqtt.js` tut) - 26 retained Nachrichten sofort empfangen.
- Vom Nutzer im Browser bestaetigt: Verbindungs-/Online-Anzeige korrekt, Ventilkacheln farbig wie erwartet, Live-Update beim Schalten eines Ventils funktioniert. Zitat: „ja, alles wie geplant!!“.
- Details siehe `docs/spec/17-webif-dashboard.md`, `docs/testing.md`.

### Hauptseiten-Redesign umgesetzt, Config/Web-Dateien-Partition endgueltig getrennt

- Nutzerwunsch: strukturiert vorgehen, erst Design der Hauptseite festlegen. Optischer Vorschlag als Artefakt vorgelegt (gleiche Farbwelt wie das bestehende Dashboard, aber grosszuegigeres Layout: Kopfzeile mit Navigation-Platzhaltern fuer Konfiguration/Programme/Zeitplan, grosser runder START/STOP-Button, groesseres Ventilraster, Programm-/Diagnostics-Karten). Vom Nutzer bestaetigt: „cooles Design, damit starten wir!“.
- Umgesetzt in `data/index.html`/`style.css`/`app.js`. Testdaten dafuer variantenreich gesetzt: 5 Programme mit unterschiedlichen Ventilkombinationen in `auto` ("Kurz", "Rasen", "Beete", "Alles", "Gewaechshaus") sowie Alias-Namen fuer alle 6 Ventile (u. a. "Kübelpflanzen", "Gewächshaus" - dabei ein reines Windows-Terminal-Encoding-Problem bei der Verifikation gefunden und durch `PYTHONIOENCODING=utf-8` behoben, die tatsaechlich gespeicherten Werte waren die ganze Zeit korrekt).
- Nutzer-Feedback nach erstem Blick: Programmname beim Start-Button fett hervorheben, zweite Fortschrittsanzeige fuer die Sequenz-Gesamtlaufzeit ergaenzen (dabei aufgefallen: die bisherige einzelne Bar zeigte eigentlich schon die Sequenz-Gesamtzeit, nicht die Ventil-Einzelzeit - korrigiert, jetzt zwei sauber getrennte, beschriftete Balken).
- **Bug erneut aufgetreten, diesmal strukturell geloest**: nach einem weiteren `uploadfs`-Lauf (fuer das Redesign) waren Programme/Aliase wieder weg - derselbe Mechanismus wie der fruehere Phase-16-Bug (uploadfs ueberschreibt immer die komplette Zielpartition), aber diesmal nicht mehr durch den `begin(false)`-Fix behebbar, da `config.json` strukturell auf derselben Partition wie die Web-Dateien lag. Vom Nutzer bemerkt: „Programme und Namen sind wieder floeten gegangen“.
- Zwei Optionen zur Wahl gestellt (sauber trennen vs. Testdaten weiter manuell nachpflegen) - Nutzer waehlt die saubere Trennung. `partitions.csv` in `webfs` (1,75 MB, Subtype `spiffs`, wird von `uploadfs` automatisch getroffen, da PlatformIO die erste Partition mit passendem Subtype waehlt) und `config` (128 KB, bewusst Subtype `0x40` statt `spiffs`, damit `uploadfs` sie nie automatisch trifft) aufgeteilt. `ConfigStore` mountet jetzt eine eigene `fs::LittleFSFS`-Instanz (`configFs`) auf `config` - dort ist `LittleFS.begin(true)` (Auto-Format) jetzt wieder sicher, da diese Partition von `uploadfs` strukturell nie beschrieben wird (der urspruengliche Bug betraf spezifisch das Zusammenspiel aus Auto-Format und einer von `mklittlefs` geschriebenen Partition). `WebManager` mountet die globale `LittleFS`-Instanz weiterhin explizit auf `webfs`.
- Einmaliger Uebergang: die Repartitionierung selbst hat die Testdaten ein letztes Mal geloescht (vorher angekuendigt). Nach erneutem Setzen per echtem `uploadfs`-Testlauf verifiziert: Programme und Aliase (inkl. Umlaute) bleiben jetzt dauerhaft erhalten, auch nach einem frischen Dashboard-Update.
- Details siehe `docs/spec/16-webif-fundament.md`, Abschnitt „Nachtrag", `partitions.csv`, `src/ConfigStore.cpp`, `src/WebManager.cpp`.

### Phase 18 (Web-Interface: Konfiguration bearbeiten) umgesetzt und getestet

- Erster Schreibpfad des Web-Interfaces (Phase 17 war rein lesend). Design vorab per Artefakt abgestimmt, iterativ verfeinert.
- `maxTime` bewusst **kein** abgeleiteter Wert: zwei automatische Berechnungsvorschlaege (Summe aller Laufzeiten + 5 min, dann laengste Einzel-Laufzeit + 5 min als schreibgeschuetzter Wert) wurden nach Nutzer-Feedback wieder verworfen - „der user muss maxtime einstellen! beruecksichtige maxtime immer". Stattdessen zeigt die UI eine gelbe Warnung + „→ X min effektiv"-Hinweis, wenn `maxTime` eine konfigurierte Laufzeit tatsaechlich deckelt (`time > maxTime`) - die Deckelung selbst laeuft unveraendert im `ValveTimer`.
- Bestaetigungs-Feedback pro Feld ebenfalls mehrfach angepasst: von einem Textbaustein („✓ gespeichert", als verwirrend verworfen) ueber ein optimistisches gruenes Aufblitzen zu der finalen Loesung - Feld faerbt sich beim Verlassen sofort rot (noch nicht per Geraete-Echo bestaetigt), blendet weich zur normalen Textfarbe zurueck sobald die Bestaetigung eintrifft. Ein dauerhaft rotes Feld zeigt automatisch eine vom Geraet abgelehnte Eingabe an, ohne eigenen Sonderfall.
- Neue `data/konfiguration.html`/`data/konfig.js`: eigene Alpine-Komponente `konfiguration()`, eigene MQTT-Verbindung (analog `app.js`), Schreibpfade `V{n}/alias/set`, `V{n}/time/set`, `V{n}/auto/set`, `main/config/set` (fuer `maxTime`, kein eigenes `main/time/set`). `data/style.css` um Formular-Basisstile (bisher nicht noetig, Phase 17 war rein lesend) und `--state-warning`-Token ergaenzt. `data/index.html`-Navigation „Konfiguration" von Platzhalter auf echten Link umgestellt.
- Schreibpfad vor dem Browser-Test wieder per `paho-mqtt` (`transport="websockets"`) verifiziert (alle 5 Set-Topics inkl. `maxTime`-Deckelungsfall), danach vom Nutzer im Browser bestaetigt: „passt alles“.
- Details siehe `docs/spec/18-webif-konfiguration.md`, `docs/testing.md`.

## Offene Punkte / nächste Schritte

- **Phase 19 (Web-Interface: Programme verwalten)** — nächster Arbeitsschritt: erstes komplexeres Datenmodell des Web-Interfaces (Array/Liste statt Einzelwerte, Teilmengen-Semantik wie bei `main/config/set`), baut auf dem in Phase 18 erprobten Schreibpfad-Muster auf.
- Phasen 20–21 (Web-Interface: Zeitplan, OTA) — geplant, hängen an Phase 19.
- Feinschliff der Statuszeilen-/Button-Abstände auf der Touch-UI-Hauptseite — funktional fertig, rein kosmetisch noch offen (Nutzer-Aussage: „lass es so, ggf. vielleicht nochmal später hübsch machen“).
- Erweiterung auf 16 Ventile (V0–V15, volle MCP23017-Kapazität) — eigene, noch nicht begonnene Phase; bekanntes Risiko: `MqttManager::parseValveTopic()` müsste für zweistellige Ventilnummern angepasst werden.
- Phase 10 (Home Assistant MQTT-Discovery) — mit Phase 15 sind alle geräteinternen Phasen (00–09, 11–15) fertig; jetzt hinter dem vorgezogenen Web-Interface (Phasen 16–21) einsortiert, danach der letzte Punkt der ursprünglichen Phasenliste.
