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
