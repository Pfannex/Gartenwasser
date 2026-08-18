# Phase 17 — Web-Interface: Status-Dashboard (read-only)

**Status:** ✅ Erledigt & getestet

## Ziel

Live-Übersicht im Browser: Ventilstatus (`V0`–`V5`), Automatik-Flags, laufende Sequenz samt Restlaufzeit, Diagnostics (`i2cStatus`/`lastError`) — inhaltliches Pendant zur Touch-UI-Hauptseite (Phase 13), aber am PC/Handy statt am Gerätedisplay. Bewusst **read-only**: erster echter Test, ob die in Phase 16 gewählte Architektur B (direkter MQTT-Zugriff des Browsers) für den Lesepfad wie gedacht funktioniert, bevor Phase 18 Schreibzugriffe hinzufügt.

## Voraussetzungen

- Phase 16 (Fundament & Architekturentscheidung) ✅
- WebSocket-Listener auf dem Mosquitto-Broker (`/etc/mosquitto/conf.d/websockets.conf`, Port 9001, `allow_anonymous true`) ✅ — vom Nutzer eingerichtet, siehe Umsetzung unten.

## Umsetzung (2026-08-17)

- **Broker-seitig** (vom Nutzer eingerichtet, Mosquitto 2.0.21 in einem Debian-LXC unter Proxmox): neue Datei `/etc/mosquitto/conf.d/websockets.conf` mit `listener 9001` / `protocol websockets` / `allow_anonymous true` (anonymer Zugriff, passend zum bestehenden 1883-Listener), `systemctl restart mosquitto`. Verifiziert per `ss -tlnp | grep 9001`.
- **Bibliotheken** (per `curl` von `unpkg.com` bezogen, lokal in `data/` abgelegt, kein CDN zur Laufzeit): `mqtt.min.js` (369 KB, MQTT.js-Browserbundle) und `alpine.min.js` (47 KB, Alpine.js 3.x CDN-Bundle). Beide zusammen ≈416 KB, passen komfortabel in die 1,875-MB-LittleFS-Partition.
- **`data/app.js`** (neu): Alpine.js-Komponente `dashboard()`, verbindet sich per `mqtt.connect("ws://192.168.1.123:9001/mqtt")` direkt mit dem Broker (Broker-Adresse fest hinterlegt, analog zu `tools/mqtt-tests/*.py` — liegt in einem anderen Netzsegment als die Geräte-IP, nicht automatisch ableitbar), abonniert `gartenwasser/#`. `handleMessage()` pflegt reaktiven State für Ventile (`V{n}/state`, `V{n}/auto/state`, `V{n}/alias`), Sequenz (`main/state`, `main/activeValve`, `main/remainingTotal`), aktives Programm (`main/program/state`) und Diagnostics (`diagnostics/i2cStatus`, `diagnostics/lastError`). `valveState()` repliziert exakt die Farblogik der Touch-UI-Ventilmatrix (`HmiManager::refreshValveStatus()`): rot = state AN (überschreibt), grün = auto AN + state AUS, dunkelgrau = auto AUS + state AUS, `V0` nie gedimmt.
- **`data/index.html`** (überarbeitet): Alpine-Template mit Kopfzeile (Verbindungsstatus Browser↔Broker + Geräte-Online-Status via `availability`), 2×3-Ventilkachel-Grid (`valve-tile`, Dashboard-Cards-Stil aus Phase 16), Sequenz-/Programm-Status-Karte, bedingt eingeblendete Fehler-Karte bei I2C-Problemen.
- **`data/style.css`**: `.header-bar`, `.valve-grid`/`.valve-tile`/`.valve-alias` ergänzt (Dashboard-Cards-Kacheln, farbcodiert wie oben).
- Keine Firmware-Änderung nötig — `WebManager` liefert die neuen Dateien unverändert über den bestehenden `serveStatic()`-Mechanismus aus.

## Betroffene Dateien

- `data/app.js` (neu)
- `data/index.html` (überarbeitet)
- `data/style.css` (ergänzt)
- `data/mqtt.min.js`, `data/alpine.min.js` (neu, Fremdbibliotheken)

## Test / Ergebnis

1. **Pipeline-Check ohne Browser**: `paho-mqtt` mit `transport="websockets"` gegen `192.168.1.123:9001/mqtt` verbunden (simuliert exakt, was `mqtt.js` im Browser tut) — Verbindung erfolgreich, 26 retained Nachrichten sofort empfangen. ✅
2. **Dateiauslieferung**: alle sechs Dateien (`/`, `/index.html`, `/style.css`, `/app.js`, `/mqtt.min.js`, `/alpine.min.js`) per `curl` mit korrekter Größe/HTTP 200 geprüft. ✅
3. **Im Browser** (Nutzer, gleiches Netz wie Broker): Seite zeigt „Verbunden“/„Gerät online“, Ventilkacheln korrekt farbig, Live-Update beim Schalten eines Ventils bestätigt. ✅ („ja, alles wie geplant!!“)

## Nachtrag (2026-08-17): Hauptseiten-Redesign + No-Cache

Nach einem optisch abgestimmten Vorschlag (Artefakt, siehe `docs/requirements.md` Entscheidungshistorie) die Hauptseite grundlegend überarbeitet: Kopfzeile mit Navigation-Platzhaltern (Konfiguration/Programme/Zeitplan, für Phasen 18–20), großer runder START/STOP-Button, größeres 4×2-Ventilraster (2 Zellen reserviert), Programm-/Diagnostics-Karten im Fußbereich. Nach Nutzer-Feedback iterativ verfeinert:

- Aktives Programm als eigene, headline-große, akzentfarbene Zeile (`.program-tag`) statt kleiner Beschriftung.
- Zwei getrennte, beschriftete Fortschrittsbalken statt einem (der ursprünglich fälschlich die Sequenz-Gesamtzeit unter dem Ventil-Label zeigte): „Restlaufzeit V1 · Apfel - 00:55“ (rot) und „Restlaufzeit Programm Rasen - 19:55“ (blau), jeweils mit „ - “ zwischen Label und Wert.
- `activeValveLabel()`/`programDetail()` (neu in `app.js`) für die konkreten Ventil-/Programmnamen statt generischer Begriffe („Ventil“/„Sequenz“).
- `WebManager` liefert alle Dateien jetzt mit `Cache-Control: no-cache, no-store, must-revalidate` aus (Nutzer bemerkte veraltete gecachte Inhalte auf dem Handy) — bewusst nur für die aktive Entwicklungsphase, vor einem produktiven Release wieder auf normales Caching umstellen.

Dabei zweimal auf Hardware reproduziert und strukturell gelöst: `pio run --target uploadfs` überschrieb wiederholt die persistierte Konfiguration, da sie zunächst weiterhin dieselbe Partition wie die Web-Dateien nutzte — siehe `docs/spec/16-webif-fundament.md`, Abschnitt „Nachtrag" (Partitionstrennung `webfs`/`config`).

## Nachtrag (2026-08-17): Dashboard interaktiv statt read-only

Auf Nutzerwunsch, noch vor den separaten Unterseiten (Phasen 18–20), die bereits sichtbaren Hauptseiten-Elemente funktional gemacht — bewusst nur die, die schon in der UI stehen (kein Formular/Editor, das bleibt Phase 18+):

- **START/STOP-Button**: `toggleSequence()` publiziert `main/cmd` (`ON`/`OFF`) direkt per `mqtt.js` — identisch zum Touch-UI-Pfad, keine eigene Logik. Die Firmware blockiert `main/cmd ON` ohnehin ohne gewähltes Programm (`MqttManager::startSequence()`), daher client-seitig keine zusätzliche Prüfung nötig.
- **Ventilkacheln `V1`–`V5`**: `toggleValve(v)` publiziert `V{n}/cmd` (`ON`/`OFF`) direkt — `V0` bleibt ohne Handler (kein eigener `cmd`, wie bei Touch-UI/MQTT).
- Hover-/Press-Feedback (`cursor: pointer`, leichte Skalierung beim Antippen) für beide.
- Vor dem Browser-Test der Schreibpfad per `paho-mqtt` (`transport="websockets"`) simuliert (identisch zu einem echten Kachel-Klick): `V1/cmd ON` → `V1/state` wechselt auf `ON`, `V1/cmd OFF` → zurück auf `OFF`. ✅

## Nachtrag (2026-08-18): "MANUELL"-Konsistenz (analog Touch-UI)

Im Zuge der Phase-14/18-Überarbeitung (siehe dortige Nachträge — manuelle `time`/`auto`-Änderungen setzen `activeProgram` jetzt zurück) auch das Dashboard nachgezogen:

- **`app.js`**: `heroHeadline()` unterscheidet jetzt drei Fälle statt zwei — läuft eine Sequenz: `"{Ventil} läuft"` (unverändert); kein Programm gewählt (`!activeProgramName`): **„Manueller Modus“** (neu, bewusst als Fließtext statt der kompakten „MANUELL“-Badge-Schreibweise, passend zur größeren Headline-Zeile neben dem START-Button); sonst weiterhin „Automatik inaktiv“ (Programm gewählt, aber nicht gestartet). Footer-Karte „Aktives Programm“ zeigt bei keiner Auswahl jetzt „MANUELL“ statt „Kein Programm gewählt“ (Wortgleichheit mit der Touch-UI-Statuszeile).
- **Ventilkacheln**: `valveMeta()` zeigt für Ventile außerhalb der Automatik jetzt die konfigurierte Laufzeit (`"{time} min"`) statt des wenig hilfreichen „nicht in Automatik“ — seit MANUELL der Normalzustand für alle Ventile ist, war dieser Text auf praktisch jeder Kachel gleichzeitig zu sehen. Dafür `handleMessage()`/das Ventil-Datenmodell um `V{n}/time/state` erweitert (bisher wurde dort nur die Restlaufzeit während des Laufens verfolgt).
- **START-Button gesperrt** (`disabled`-Attribut, `:disabled="!sequenceRunning && !activeProgramName"`), solange kein Programm gewählt ist — vorher passierte bei einem Klick in diesem Zustand gar nichts sichtbares (die Firmware weist `main/cmd ON` still ab). Neue CSS-Regel `.hero-btn:disabled` (grau, `cursor: not-allowed`, kein Press-Scale-Effekt). Analog zur gleichzeitig umgesetzten Sperre auf dem Touch-Display (siehe `docs/spec/13-touch-ui.md`, Nachtrag).

Auf Hardware/im Browser vom Nutzer bestätigt.

## Nachtrag (2026-08-18): "Nächster Termin" + Hero-Neugestaltung mit Programm-Picker

Direkt im Anschluss an Phase 20 (Zeitplan verwalten) zwei Runden Weiterentwicklung:

**Runde 1 — "Nächster Termin"-Anzeige**: Nutzerwunsch, den nächsten fälligen Zeitplan-Start auf der Hauptseite sichtbar zu machen. Bewusst **rein browserseitig** berechnet (`app.js`, neue `nextSchedule()`/`nextOccurrence()`/`formatWhen()`) — abonniert zusätzlich `main/schedule/state`, ermittelt für jeden aktiven, nicht pausierten Eintrag mit gültiger Programm-Referenz die nächste Fälligkeit (`daily`/`weekly`/`once`) und zeigt das Minimum. Ausdrücklich **kein** Firmware-Eingriff, anders als der am 2026-08-17 verworfene „Kollisions-Hinweis“ (siehe `docs/spec/15-wochenplan.md`, Abschnitt „Verworfen“) — jener war eine echtzeit-blockierende Server-Prüfung für `main/cmd ON`, das hier ist nur eine Anzeige, ungleich einfacher und ohne die damalige Aufwand/Nutzen-Abwägung. Anzeige: „Heute“/„Morgen“/Wochentag (< 7 Tage)/volles Datum, je nachdem wie weit der Termin entfernt ist.

**Runde 2 — Hero-Bereich neu strukturiert**, nach Nutzer-Vorschlag mehrerer zusammenhängender Vereinfachungen:
- **Programm-Auswahl direkt im Hero** (`program-picker`): Klick auf den Programmnamen (bzw. „Programm wählen“, wenn keins gewählt ist) öffnet eine Dropdown-Liste aller Programme + „Kein Programm“ — Auswahl wendet sofort an (`main/program/cmd`, identisch zu „Aktivieren“ auf der Programme-Seite, kein Bestätigungsschritt). Während die Sequenz läuft gesperrt (`togglePicker()` no-op, `.program-picker.disabled`) — ein Wechsel wäre bis zum nächsten Start ohnehin wirkungslos (`startSequence()` wendet das Programm erneut an, siehe `docs/spec/14-programme.md`), analog zur bestehenden Sperre des Programme-Buttons im Touch-UI.
- **„Automatik inaktiv“/„Manueller Modus“-Headline komplett entfernt** — der Picker selbst zeigt den Zustand bereits eindeutig („Programm wählen“ vs. Programmname), ein zusätzlicher Textbaustein war redundant. `heroHeadline()` vereinfacht auf den einzig verbleibenden Fall (`"{Ventil} läuft"`, nur noch während `sequenceRunning` gerendert).
- **„Nächster Termin“ zieht in den Hero um** (aus Runde 1), dort aber nur sichtbar, wenn **keine** Sequenz läuft — macht während des Laufs Platz für die beiden Fortschrittsbalken.
- **Fußbereich radikal verschlankt**: die Karten „Aktives Programm“ (jetzt im Hero-Picker) und „Nächster Termin“ (jetzt im Hero) entfallen ersatzlos, „Diagnostics“ bleibt als einzige Karte übrig und läuft jetzt über die volle Breite statt in einem Grid (`.footer-grid` als tote Regel entfernt, ebenso das nur dort verwendete `.footer-card .row-head` und ein länger unbemerkt totes, von Phase 19 überschriebenes Duplikat von `.program-name`/`.program-detail`).
- **Backlog-Punkt notiert, nicht umgesetzt**: Nutzer schlug zusätzlich eine Live-Ansicht des seriellen ESP-Logs als weitere Dashboard-Karte vor. Geprüft: `Logger` hat aktuell **keinen** MQTT-Publish-Pfad (nur Serial + `diagnostics/lastError` für `ERROR`-Einträge) — bräuchte eigene Firmware-Anbindung (neues Topic, Log-Level-Filterung, Ringpuffer-Dimensionierung im begrenzten RAM, bewusst nicht retained wegen Flut-Gefahr). Deutlich größerer Aufwand als der Rest dieser Überarbeitung, daher bewusst als eigener, noch nicht begonnener Backlog-Punkt zurückgestellt statt hier mit umgesetzt — siehe `docs/Log.md`, „Offene Punkte“.

Auf Hardware/im Browser vom Nutzer bestätigt: „Passt alles“.

## Nachtrag (2026-08-18): Live-Log als eigene Seite umgesetzt

Der in Runde 2 oben zurückgestellte Backlog-Punkt wurde direkt im Anschluss doch noch umgesetzt — vorher aber, auf Nutzerwunsch, erst die zugrunde liegende `Logger`-Nutzung überarbeitet (neue `Source::VALVE`/`SEQ`, HMI-Aktionen geloggt, siehe `docs/requirements.md`, Abschnitt „Log-Format“). Danach iterativ über viele Nutzer-Feedback-Runden entwickelt:

- **Firmware**: `Logger::setLineCallback()` (neuer Hook, identisches Muster zu `setErrorCallback()`) reicht jede Zeile außer `PUB`/`SUB` an `MqttManager` weiter, das sie roh (nicht über `publishAndLog()`, sonst Rückkopplung) auf `gartenwasser/diagnostics/livelog` publiziert (nicht retained). Ein immer aktiver 80-Zeilen-Ringpuffer plus Replay (automatisch nach jedem Connect, zusätzlich auf Anfrage über `gartenwasser/diagnostics/livelog/replay`) sorgt dafür, dass eine neu geöffnete Log-Seite nicht leer bleibt, obwohl der Stream selbst nicht retained ist.
- **Erste Version**: Karte auf dem Dashboard — nach Rückmeldung „nach dem Wechsel der Seite ist das Log leer" (das Kernproblem des fehlenden Replays) durch obige Puffer-/Replay-Logik gelöst.
- **Umzug auf eigene Seite** `data/log.html`/`data/log.js` (eigener Nav-Tab „Log", damit ist die Hauptnavigation aller fünf Seiten komplett aktiv) — schlanke eigene MQTT-Verbindung, abonniert nur `diagnostics/livelog` + `availability` statt des kompletten Wildcard-Topics.
- **Tabellenansicht** (Zeit/Quelle/Typ/Event-Spalten) statt loser Textzeilen. Quelle/Typ als Spaltenkopf mit Dropdown-Checkliste, Mehrfachauswahl — Facetten-Prinzip (leere Auswahl = keine Einschränkung, ausgewählte Werte schränken ein), nach einer Iterationsrunde korrigiert (ursprünglich Ausschluss-Toggle mit allem-aktiv-Default, vom Nutzer als unerwartet zurückgemeldet).
- **Event-Spalte als Sucheingabe**: Klick auf „Event" macht die Spaltenüberschrift zum Texteingabefeld (ersetzt eine anfänglich separate Suchleiste über der Tabelle), filtert live bei jedem Tastendruck. Nach Verlassen des Feldes (Blur oder Enter, beides gleichbedeutend) klappt es zu einem Label „Eventfilter: *Begriff*" zusammen (inkl. der vom Nutzer gewünschten Wildcard-Sternchen), mit eigenem Lösch-Button (×). `autocapitalize`/`autocomplete`/`autocorrect`/`spellcheck` am Eingabefeld deaktiviert.
- **Drei CSS-Bugs unterwegs gefunden und behoben** (alle durch Nutzer-Screenshots aufgedeckt): (1) `overflow: hidden` auf der äußeren Karte schnitt die Filter-Dropdowns ab — behoben durch gezieltes Eckenabrunden der äußersten Kindelemente statt eines pauschalen `overflow: hidden` auf der Karte. (2) Kopf- und Körpertabelle mussten strukturell getrennt werden (statt `position: sticky` im `thead` innerhalb des scrollenden Bereichs) — ein durch Filterung schrumpfender Scrollbereich schnitt das Dropdown sonst ebenfalls ab; beide Tabellen teilen sich dieselben `<colgroup>`-Spaltenbreiten, um optisch ausgerichtet zu bleiben. (3) Das geerbte `text-transform: uppercase` der Spaltenkopf-Beschriftungen verfälschte versehentlich auch den eingetippten Suchbegriff in der Anzeige (`0x` → `0X`, der gespeicherte Wert war die ganze Zeit korrekt) — per gezieltem `text-transform: none` auf das Such-Label behoben.
- Auf Hardware verifiziert: keine `PUB`/`SUB`-Einträge im Live-Log, keine Flut/Rückkopplung (Testlauf mit gemischten Aktionen), Boot-Sequenz wird nach echtem Hardware-Reset korrekt nachgeliefert, Anfrage-Replay liefert auch ohne frische Verbindungslücke. Details siehe `docs/testing.md`.

Vom Nutzer abschließend bestätigt: „sehr geil, läuft jetzt perfekt!“.

### Nachtrag (2026-08-18): Topic-Name korrigiert

Das Live-Log-Topic hieß zunächst `diagnostic/livelog` (Singular) — auf ausdrücklichen Nutzerwunsch ursprünglich so vorgegeben, aber inkonsistent zu den bestehenden `diagnostics/i2cStatus`/`diagnostics/lastError`. Vom Nutzer selbst als eigener Fehler erkannt und auf `diagnostics/livelog` (+ `.../replay`) korrigiert, jetzt Teil der bestehenden `diagnostics/`-Gruppe statt eines eigenen Astes in der Topic-Struktur (siehe `docs/requirements.md`).
