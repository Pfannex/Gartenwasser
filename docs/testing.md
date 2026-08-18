# Testing — Gartenwasser

Zentrale, strukturierte Übersicht aller Testläufe. Ergänzt die phasenweisen `Test`/`Test / Ergebnis`-Abschnitte in `docs/spec/*.md` (dort stehen die Details je Phase), hier steht der schnelle Gesamtüberblick.

## Wie getestet wird

- Testskripte liegen unter [`tools/mqtt-tests/`](../tools/mqtt-tests/) (siehe dortige `README.md` für Voraussetzungen/Nutzung).
- Für manuelle/visuelle Kontrolle: [`tools/mqtt-spy/`](../tools/mqtt-spy/) enthält eine versionierte Kopie der [mqtt-spy](https://kamilfb.github.io/mqtt-spy/)-Konfiguration (Windows-Java-Tool), sinnvoll mit `+`-Wildcards nach Topic-Gruppen sortiert.
- Automatisiert per Python + [paho-mqtt](https://pypi.org/project/paho-mqtt/) gegen den echten Broker (`192.168.1.123:1883`) und die echte Hardware — kein Mock/Simulator.
- Skripte abonnieren `gartenwasser/#`, publizieren Testbefehle und prüfen die resultierenden (retained) States gegen Erwartungswerte.
- Wo nötig, wird ein echter Hardware-Reset über `esptool --after hard-reset` ausgelöst (RTS-Pin-Puls, kein Reflash) statt eines simulierten Neustarts.
- Vor/nach jedem Testlauf wird der bestehende Konfigurationsstand (`config`, `programs`) gesichert und am Ende wiederhergestellt — Testläufe hinterlassen keine dauerhaften Änderungen am Gerät.
- Zwei Prüfpunkte der Checkliste erfordern physischen Eingriff (I2C-Kabel ziehen, WLAN/MQTT trennen) — die wurden bewusst nicht unbeaufsichtigt automatisiert, sondern separat interaktiv nachgeholt (Nutzer führt die physische Aktion aus, ein Live-MQTT-Mitschnitt wertet das Ergebnis aus, siehe Punkte 6 und 9 unten).

## Regressionstest (Checkliste aus Phase 12) — 2026-08-16

Zusammenhängender Gesamtdurchlauf. **Alle 10 Checklistenpunkte abgedeckt** (8 automatisiert, 49 Einzel-Checks, 48 PASS + 1 Skript-Timing-Artefakt; 2 davon — Punkte 6 und 9 — interaktiv mit physischem Eingriff nachgeholt).

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Boot / Verfügbarkeit | `availability = online` geprüft, `main/config/state` sofort (retained) verfügbar | 2/2 PASS | ✅ |
| 2 | Ventile manuell + V0-Kopplung | V1/V2 einzeln und gemeinsam ON/OFF; V0 bleibt an, solange mind. ein Ventil aktiv ist | 5/5 PASS | ✅ |
| 3 | Laufzeit | `time/set`, `maxTime`-Deckelung (`min(time, maxTime)`, mit `time=5`/`maxTime=1` erzwungen), echter Zeitablauf nach 60 s (kürzeste zulässige Laufzeit) inkl. automatischer Abschaltung und Re-Armierung auf `time` | 5/6 PASS | ⚠️ 1 Skript-Timing-Artefakt, kein Firmware-Fehler (siehe unten) |
| 4 | Automatik-Flag | `auto/set` ON/OFF → `auto/state` | 2/2 PASS | ✅ |
| 5 | Automatik-Sequenz | `main/cmd ON` startet mit erstem `auto=ON`-Ventil; manuelles `ON` eines nicht beteiligten Ventils wird ignoriert; manuelles `OFF` des aktiven Ventils rückt die Sequenz vor (Restlaufzeit bleibt `00:00`); `main/cmd OFF` bricht sofort ab und armiert alle Ventile neu | 9/9 PASS | ✅ |
| 6 | Diagnostics-Fehlerfall | I2C-Bus manuell kurz getrennt (Nutzer), per Live-MQTT-Mitschnitt ausgewertet: `i2cStatus = error` + `lastError` gesetzt, nach Wiederverbinden `ok` | 1/1 PASS | ✅ interaktiv nachgeholt |
| 7 | Alias (inkl. V0) | Umlaute (UTF-8) funktionieren; zu lange Werte (> 32 Zeichen) und Werte mit Steuerzeichen werden abgelehnt (unverändert) | 4/4 PASS | ✅ |
| 8 | Konfiguration per JSON | `main/config/set` Teil-Update ändert nur angegebene Felder; unbekannter Key (`V9`) wird ignoriert, Board bleibt responsiv | 3/3 PASS | ✅ |
| 9 | Resilienz | Automatik-Sequenz gestartet, Nutzer trennt WLAN am Router manuell (~49 s Ausfall), per Live-MQTT-Mitschnitt ausgewertet: Restlaufzeit lief lokal exakt korrekt weiter (kein Pausieren/Reset), Sequenz-Übergang zur berechneten Sekunde, nach Reconnect alle States korrekt neu publiziert | 1/1 PASS | ✅ interaktiv nachgeholt |
| 10 | Persistenz | Echter Hardware-Reset (`esptool --after hard-reset`) → `time`/`auto`/`alias` und Programme-Liste überleben, alle Ventile AUS, keine Automatik-Sequenz startet automatisch | 6/6 PASS | ✅ |

**Anmerkung zu Punkt 3**: Die eine "fehlgeschlagene" Prüfung erwartete exakt `remaining = 01:00` nach dem Einschalten, gemessen wurde `00:59`. Ursache: zwischen Einschalten und Prüfung (1,5 s Wartezeit im Testskript) war bereits ein Sekunden-Tick (1000 ms-Takt) vergangen. Die eigentliche Prüfung — Deckelung auf `maxTime` statt der vollen `time` (`00:59`/`01:00` statt `05:00`) — war korrekt. Kein Firmware-Fehler, nur eine zu strenge Testskript-Assertion (inzwischen auf `{"01:00", "00:59"}` korrigiert).

**Details zu Punkt 9 (Resilienz)**: Automatik gestartet mit V1 (3 Min) → V2 (2 Min) um `18:38:19`. Nutzer trennt WLAN am Router; Board meldet selbst `18:39:39 Verbindung verloren.` (automatisch in `lastError`). Nach Wiederverbindung um `18:40:36` zeigt `V1/time/remaining = 00:43` — exakt der rechnerisch korrekte Wert (180 s − 137 s reale Elapsed-Zeit), keine Pause/kein Reset durch den ~49-sekündigen Ausfall. Übergang V1→V2 erfolgte um `18:41:19`, exakt 3:00 nach Start, auf die Sekunde genau. Bestätigt die Kernanforderung „läuft lokal/autonom weiter" (siehe `docs/requirements.md`) empirisch mit Zeitstempeln.

Alle 10 Punkte damit vor dem produktiven Einsatz abgedeckt.

## Phase 14 — Bewässerungsprogramme — 2026-08-16

**14 von 14 Checks bestanden** (6 Testfälle aus `docs/spec/14-programme.md` + 1 Bonus-Test).

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Bulk-Replace (`main/programs/set`) | 4-Programme-Array gesendet, `main/programs/state` zeigt exakt diese 4 Programme; `main/config/state` bleibt unberührt | 2/2 PASS | ✅ |
| 2 | Index-Auswahl (`main/program/cmd 4`) | Wendet Programm „Test" an → alle `V1`-`V5` `time`/`auto` korrekt übernommen, `main/program/state` zeigt `{"index":4,"name":"Test"}` | 3/3 PASS | ✅ |
| 3 | Manuelle Änderung während Programm aktiv | `V2/time/set` während Programm 4 aktiv → Wert übernommen, Programmwahl bleibt bestehen (kein Lock) | 2/2 PASS | ✅ |
| 4 | Ungültiger Index (`cmd 99`) | Wird ignoriert, `main/program/state` unverändert | 1/1 PASS | ✅ |
| 5 | Auswahl löschen (`cmd 0`) | `main/program/state` → `{"index":0,"name":null}`, Ventile bleiben unangetastet | 2/2 PASS | ✅ |
| 6 | Bonus: Teilmengen-Semantik | Programm, das ein Ventil in keinem Feld (`time`/`auto`) erwähnt, lässt dessen Zustand vollständig unverändert | 4/4 PASS | ✅ |

**Bug gefunden und gefixt**: `main/programs/set` verursachte einen Stack-Overflow-Absturz (`Guru Meditation Error: Core 0 panic'ed (Stack protection fault)` in der `loopTask`, per Serial-Monitor-Mitschnitt bestätigt). Ursache: mehrere 2048-Byte-JSON-Puffer gleichzeitig auf dem mit 8192 Byte knapp bemessenen Standard-Stack. Fix: `SET_LOOP_TASK_STACK_SIZE(16 * 1024)` in `main.cpp`. Nach Re-Flash liefen alle Tests sauber durch — siehe `docs/spec/14-programme.md` für Details.

## Phase 14 — `P1`–`P4`-Touch-UI-Anbindung + Programm-Pflicht — 2026-08-16

Interaktiv getestet (Nutzer bedient das Touch-Display, Ergebnis per gezielten MQTT-Abfragen/Live-Mitschnitten gegengeprüft statt vollautomatisiertem Skript) — **alle Fälle bestanden**.

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | `P1`/`P2` wenden Programm an | Programme „Kurz“ (`shortcut:"P1"`)/„Rasen“ (`shortcut:"P2"`) angelegt, Button gedrückt → `main/program/state` korrekt, `V1/time`/`auto` übernommen | PASS | ✅ |
| 2 | Duplikat-Log gekürzt | Zwei Programme mit `shortcut:"P1"` gesendet → `lastError` zeigt jetzt vollständig `"Shortcut P1 doppelt belegt!"` statt abgeschnittenem Text | PASS | ✅ |
| 3 | `P3`/`P4` ohne Bindung | Gedrückt, ohne dass ein Programm diesen `shortcut` hat → keine Auswahl ändert sich (bestätigt vom Nutzer: „gut ist, dass sich P3/P4 nicht anwählen lassen“) | PASS | ✅ |
| 4 | Hinweis „P{n} nicht konfiguriert!“ | Nach Druck auf unkonfigurierten Button erscheint der Hinweis (orange, 2s) und verschwindet danach wieder | PASS | ✅ |
| 5 | Programm-Toggle (Abwahl) | Aktives Programm erneut per Button gedrückt → `main/program/state` zurück auf `{"index":0,"name":null}` (bestätigt per MQTT-Abfrage nach Nutzeraktion) | PASS | ✅ |
| 6 | 8 Programme, aktives ohne Button-Bindung | 8 Programme geladen (4 mit `shortcut`, 4 ohne), Programm 5 („Sommer“, kein `shortcut`) aktiviert → kein `P1`-`P4` leuchtet, Statuszeile zeigt trotzdem „Programm: Sommer“ (vom Nutzer am Display bestätigt) | PASS | ✅ |
| 7 | `main/cmd ON` ohne Programm blockiert | Programmwahl auf `0` gesetzt, `main/cmd ON` per MQTT gesendet → `main/state` bleibt `OFF`, `lastError` zeigt `"main/cmd ON ignoriert: kein Programm gewaehlt."` | PASS | ✅ |
| 8 | `main/cmd ON` mit Programm funktioniert normal | Programm 1 gewählt, `main/cmd ON` gesendet → `main/state=ON`, `activeValve=V1`, sauber mit `main/cmd OFF` gestoppt | PASS | ✅ |
| 9 | „MANUELL“ statt „Bereit“ | Kein Programm gewählt, keine Aktivität → Statuszeile zeigt „MANUELL“ (vom Nutzer am Display bestätigt: „schaut gut aus“) | PASS | ✅ |

**Design-Iteration während des Tests**: Ursprünglich war „Automatik ohne Programm“ nur mit einem nicht-blockierenden Hinweis vorgesehen (main/cmd startet trotzdem). Nach dem ersten Hardware-Test („es läuft auch V1 los, das darf dann nicht sein“) wurde gemeinsam entschieden, `main/cmd ON` stattdessen komplett zu blockieren, wenn kein Programm gewählt ist — siehe `docs/requirements.md`, Entscheidungshistorie 2026-08-16, und `docs/spec/07-automatik-sequenz.md`.

**Nebenbei gefundener Fehler in der eigenen Test-Vorbereitung**: ein erster Versuch, den Resilienztest-Monitor neu zu starten, scheiterte, weil zwei Monitor-Prozesse mit identischer MQTT-Client-ID gleichzeitig liefen (siehe „Manuelle Regressionstest-Punkte“ oben) — hier nochmal relevant, da dieselbe Ursache auch bei einem der Beobachtungsfenster für diesen Test zunächst zu leeren Ergebnissen führte (Fenster lief ab, bevor der Nutzer die Aktion ausgeführt hatte). Behoben durch Wechsel von zeitgesteuerten Beobachtungsfenstern auf einmalige Statusabfragen nach expliziter Nutzerbestätigung.

## Phase 15 — Zeitplan/Scheduler — 2026-08-16

Automatisiert per Python/paho-mqtt gegen den echten Broker getestet (Skript ad hoc erstellt, nicht dauerhaft im Repo) — **alle Fälle bestanden**, inkl. eines echten Zeit-Tests.

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Bulk-Set aller Trigger-Typen | Je ein `daily`/`weekly`/`once`-Eintrag gesendet → `main/schedule/state` zeigt alle drei korrekt inkl. typ-spezifischer Felder (`weekdays`/`date`) | PASS | ✅ |
| 2 | Globaler Schalter | `main/schedule/cmd OFF`/`ON` → `enabled` in `main/schedule/state` wechselt entsprechend | PASS | ✅ |
| 3 | Einzelvalidierung | 5 Einträge gemischt gesendet (fehlendes `program`, unbekannter `type`, ungültige `time`, unbekannter Wochentag, ein gültiger) → nur der gültige bleibt übrig, `diagnostics/lastError` zeigt die Ablehnung | PASS | ✅ |
| 4 | Cleanup-Funktion | `main/schedule/cleanup` mit einem abgelaufenen (`2020-01-01`) und einem zukünftigen (`2099-01-01`) `once`-Eintrag plus einem `daily`-Eintrag → nur der abgelaufene wird entfernt | PASS | ✅ |
| 5 | **Echter Trigger-Test** | `daily`-Eintrag auf eine Uhrzeit 2 Minuten in der Zukunft gesetzt, ~130s gewartet → feuerte exakt zur Minute (`main/state=ON`, `activeValve=V1`, `main/program/state` zeigt das referenzierte Programm „Kurz" korrekt angewendet) | PASS | ✅ |

Testzustand danach vollständig zurückgesetzt (Sequenz gestoppt, Test-Zeitplan geleert, Programmwahl zurückgesetzt). Kollisions-Hinweis bei manuellem `main/cmd ON` nahe am nächsten Trigger bewusst noch nicht umgesetzt (siehe `docs/spec/15-wochenplan.md`, „Noch nicht umgesetzt") — daher auch nicht getestet.

### Erneuter Trigger-Test nach der Touch-UI-Neugestaltung — 2026-08-17

Nach dem kompletten HMI-Umbau (`shortcut`-Feld entfernt, `kMaxPrograms` 32) nochmal end-to-end auf Hardware verifiziert, dass der Scheduler mit dem neuen Programm-Bestand weiterhin sauber funktioniert: `once`-Eintrag auf `18:00` (5 Minuten in der Zukunft) gesetzt, referenziert Programm „V1" (eines der 5 zu diesem Zeitpunkt geladenen Testprogramme, siehe oben, „5 Programme laden + durchblättern"). Trigger feuerte pünktlich, Programm „V1" wurde angewendet (`main/program/state` zeigte `{"index":1,"name":"V1"}`), Sequenz lief die konfigurierte 1 Minute durch und beendete sich danach selbstständig (`main/state` zurück auf `OFF`). Vom Nutzer am Display live mitverfolgt und bestätigt („sauber angelaufen"). PASS ✅.

## Touch-UI-Neugestaltung (Nachtrag zu Phase 13) — 2026-08-17

Interaktiv getestet: jede Layout-/Funktionsänderung wurde gebaut, geflasht und direkt am Gerät begutachtet (kein automatisiertes Skript, reine UI/Bedienbarkeits-Prüfung) — Kernfunktionen bestätigt, Feinschliff der Abstände/Textposition bewusst als kosmetisch offen gelassen (siehe `docs/spec/13-touch-ui.md`, Nachtrag).

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | START/STOP-Button | Funktion unverändert zu vorherigem AUTO/OFF-Button, nur Beschriftung geändert | Bestätigt am Gerät | ✅ |
| 2 | Ventil-Statusmatrix, Farblogik | Grün/Dunkelgrau/Rot-Logik (auto/state) unverändert in der neuen 4×4-Matrix sichtbar, `V0` nie gedimmt | Bestätigt am Gerät | ✅ |
| 3 | Ventile per Matrix-Tap schalten | `V1`–`V5` antippen schaltet direkt (`V{n}/cmd`), inkl. Sperre für manuelles ON während laufender Automatik; `V0` ohne Reaktion | Bestätigt am Gerät | ✅ |
| 4 | Programme-Button zeigt aktives Programm | Buttontext wechselt korrekt bei Auswahl über Touch, MQTT `main/program/cmd` und `main/programs/set` | Bestätigt am Gerät | ✅ |
| 5 | Programme-Unterseite: Blättern | `<`/`>` blättern durch alle geladenen Programme inkl. „Kein Programm“, startet beim Öffnen immer beim aktiven Programm | Bestätigt am Gerät | ✅ |
| 6 | Programme-Unterseite: OK wendet nur an | OK wählt das durchgeblätterte Programm, startet **nicht** automatisch — Start bleibt separater Schritt auf der Hauptseite | Bestätigt am Gerät | ✅ |
| 7 | Programme-Unterseite: Abbrechen | Kehrt ohne Änderung der Programmwahl zur Hauptseite zurück | Bestätigt am Gerät | ✅ |
| 8 | Statuszeile: Fehler | I2C-Fehler zeigt roten Hintergrund + gelbe Schrift (nicht selbst provoziert, Logik code-seitig verifiziert) | Nicht am Gerät provoziert, nur Code-Review | ⚠️ noch nicht hardware-verifiziert |
| 9 | Statuszeile: laufende Automatik | Zeile 1 „`V{n} mm:ss \| mm:ss`“ (Ventil-Restlaufzeit \| Sequenz-Restlaufzeit gesamt), Zeile 2 Alias-Name | Bestätigt am Gerät | ✅ |
| 10 | Statuszeile: manuelles Ventil | Beim Schalten eines Ventils per Matrix-Tap (außerhalb der Sequenz) zeigt Zeile 1 „MANUELL“, Zeile 2 den Alias-Namen | Bestätigt am Gerät | ✅ |
| 11 | `P1`–`P4` entfernt | Keine physischen Shortcut-Buttons mehr vorhanden, Programme nur noch über die neue Unterseite erreichbar | Bestätigt am Gerät | ✅ |
| 12 | 32 Programme statt 8 | `ConfigStore::kMaxPrograms` auf 32 angehoben, kein Funktionstest mit tatsächlich 32 Programmen durchgeführt | Nicht mit voller Anzahl getestet | ⚠️ nur Code-Review |
| 13 | 5 Programme laden + durchblättern | Per MQTT 5 Programme gesetzt (je ein Ventil auf `auto`+1 Min.), am Gerät in der Programme-Unterseite durchgeblättert | Bestätigt am Gerät („werden korrekt geladen!“) | ✅ |
| 14 | „Kein Programm“ setzt `auto` aller Ventile zurück | `main/program/cmd 0` (bzw. OK auf „Kein Programm“) → alle `V{n}/auto/state` gehen auf `OFF`, nicht nur die Auswahl-Anzeige | Bestätigt per MQTT-Abfrage (`V1`–`V5` alle `OFF`) | ✅ |
| 15 | Programme-Button gesperrt während Automatik | Bei laufender Sequenz ist der Programme-Button nicht mehr antippbar (`LV_STATE_DISABLED`) | Bestätigt am Gerät | ✅ |
| 16 | Press-Feedback aller Touch-Buttons | START, Ventilmatrix, Programme, `<`/`>`/OK/Abbrechen werden beim Antippen kurz weiß | Bestätigt am Gerät | ✅ |

**Nicht Teil dieser Testrunde**: Zeitplan (Phase 15) manueller Hardware-Test — stand als Merker aus, wurde bewusst auf „nach dem HMI-Umbau“ verschoben und ist damit jetzt als Nächstes fällig (siehe `docs/Log.md`, „Offene Punkte“).

## Phase 16 — Web-Interface-Fundament — 2026-08-17

Auf Hardware getestet (Build→Flash→curl/Browser-Prüfung je Änderung).

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Seite lädt über Geräte-IP | `http://<IP>/`, `/index.html`, `/style.css` per `curl` geprüft | HTTP 200, korrekter Inhalt | ✅ |
| 2 | Flash-Größen-Checkpoint | Vorher/Nachher-Vergleich `pio run`-Ausgabe | 41,5 % → 43,1 % (≈+45 KB, deutlich unter der Schätzung) | ✅ |
| 3 | Persistenz-Regression (SPIFFS→LittleFS) | Alias-Wert per MQTT gesetzt, Hardware-Reset (`esptool --after hard-reset`), Wert erneut abgefragt | Wert korrekt erhalten geblieben | ✅ |
| 4 | `uploadfs`-Kompatibilität (1. Versuch, `LittleFS.begin(true)`) | `pio run --target uploadfs` geschriebene Dateien nach Boot per `curl` geprüft | **Fehlgeschlagen** — Dateien nach erstem Boot nicht mehr vorhanden (Auto-Format erkannte gültiges Image fälschlich als ungültig) | ❌ Bug gefunden |
| 5 | `uploadfs`-Kompatibilität (Fix, `LittleFS.begin(false)`) | Frisches `uploadfs`-Image, Reboot, `curl`-Prüfung wiederholt | Dateien sofort korrekt vorhanden, kein Reformat | ✅ Bug behoben |
| 6 | Persistenz-Regression, erneut mit finalem Fix | Alias-Wert per MQTT gesetzt, Reboot, Wert erneut abgefragt | Wert korrekt erhalten geblieben | ✅ |

**Nebenbefund**: der ursprüngliche `LittleFS.begin(true)`-Versuch (Prüfpunkt 4) hat die zu diesem Zeitpunkt gesetzten Testdaten (5 Programme, 1 Zeitplan-Eintrag, Config-Werte) gelöscht — nicht vorher angekündigt, im Nachhinein vom Nutzer als unkritisch bestätigt (reine Testdaten dieser Session). Mit dem finalen Fix (`begin(false)`) tritt dieses Problem nicht mehr auf.

## Phase 17 — Web-Interface: Status-Dashboard — 2026-08-17

Broker-seitiger WebSocket-Listener vom Nutzer eingerichtet und verifiziert (`ss -tlnp | grep 9001`), danach Dashboard implementiert und getestet.

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | MQTT-over-WebSocket-Pipeline (ohne Browser) | `paho-mqtt` mit `transport="websockets"` gegen `192.168.1.123:9001/mqtt` verbunden, simuliert `mqtt.js` | Verbindung erfolgreich, 26 retained Nachrichten sofort empfangen | ✅ |
| 2 | Dateiauslieferung | `/`, `/index.html`, `/style.css`, `/app.js`, `/mqtt.min.js`, `/alpine.min.js` per `curl` geprüft | HTTP 200, korrekte Größe je Datei | ✅ |
| 3 | Verbindungs-/Online-Anzeige im Browser | Seite geöffnet (gleiches Netz wie Broker) | „Verbunden“/„Gerät online“ korrekt angezeigt | ✅ |
| 4 | Ventilkacheln, Farblogik | Sichtprüfung im Browser | Farbig wie erwartet (grün/grau/rot, deckungsgleich zur Touch-UI-Matrix) | ✅ |
| 5 | Live-Update | Ventil geschaltet, Browser beobachtet | Anzeige aktualisiert sich sofort | ✅ |

Vom Nutzer bestätigt: „ja, alles wie geplant!!“.

## Config/Web-Dateien-Partitionstrennung — 2026-08-17

Auslöser: `uploadfs`-Läufe während der Hauptseiten-Redesign-Iteration löschten wiederholt die persistierte Konfiguration (`config.json`/`programs.json`/`schedule.json` lagen auf derselben Partition wie die Web-Dateien). Fix: `partitions.csv` in `webfs`/`config` aufgeteilt.

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Partitionierung korrekt geflasht | `pio run -t upload -v` — Flash-Adressen von `bootloader.bin`/`partitions.bin`/`firmware.bin` geprüft | Alle drei Regionen (inkl. neuer Partitionstabelle) korrekt geschrieben | ✅ |
| 2 | `uploadfs` trifft nur `webfs` | Flash-Adressbereich der `uploadfs`-Ausgabe geprüft | `0x610000`–`0x7cffff` (= `webfs`), `config`-Partition (`0x7D0000`+) unberührt | ✅ |
| 3 | Boot nach Repartitionierung | Seriellen Boot-Log geprüft | Kein Mount-Fehler, beide Partitionen (`config` via Auto-Format, `webfs` via bestehendes Image) erfolgreich gemountet | ✅ |
| 4 | Web-Dateien weiterhin ausgeliefert | `/`, `/index.html`, `/style.css`, `/app.js`, `/mqtt.min.js`, `/alpine.min.js` per `curl` | HTTP 200, korrekte Inhalte | ✅ |
| 5 | **Entscheidender Test**: Config übersteht `uploadfs` | Testdaten (5 Programme, 6 Aliase inkl. Umlaute) gesetzt, `pio run -t uploadfs` erneut ausgeführt, Daten per MQTT erneut abgefragt | Programme und Aliase vollständig und korrekt erhalten geblieben | ✅ |

Einmaliger Nebeneffekt der Repartitionierung selbst (Partitionsgrenzen verschieben sich): Testdaten gingen ein letztes Mal verloren, danach erneut gesetzt — ab jetzt bleiben sie bei jedem künftigen `uploadfs`-Lauf erhalten.

## Phase 18 — Web-Interface: Konfiguration bearbeiten — 2026-08-17

Erster Schreibpfad des Web-Interfaces (bisher nur Phase 17, rein lesend). Design vorab per Artefakt abgestimmt (`maxTime` bleibt manuell, gelbe Warnung bei Deckelung, rotes Bestätigungs-Feedback).

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Dateiauslieferung | `/konfiguration.html`, `/konfig.js` per `curl` geprüft | HTTP 200, korrekter Inhalt | ✅ |
| 2 | Alias setzen | `V3/alias/set "TestAlias"` (simuliert Feld-`blur()`) | `V3/alias` echot `TestAlias` | ✅ |
| 3 | Laufzeit setzen | `V3/time/set "7"` | `V3/time/state` echot `7` | ✅ |
| 4 | Automatik-Flag umschalten | `V3/auto/set` (Toggle-Klick) | `V3/auto/state` echot neuen Wert | ✅ |
| 5 | `maxTime` setzen | `main/config/set {"maxTime":12}` | `main/time/maxTime` echot `12` | ✅ |
| 6 | Laufzeit über `maxTime` gesetzt | `V3/time/set "20"` bei `maxTime=12` | `V3/time/state` zeigt weiterhin `20` (voller Sollwert, Deckelung wirkt erst beim Einschalten auf `time/remaining`) | ✅ |
| 7 | Im Browser | Seite geöffnet, Felder bearbeitet | Rot beim Verlassen des Feldes, weiches Zurückblenden nach Bestätigung, gelbe Deckelungs-Warnung korrekt | ✅ |

Testdaten (`V3`, `maxTime`) nach Punkt 6 auf Ausgangswerte zurückgesetzt. Vom Nutzer bestätigt: „passt alles“.

## Phase 19 — Web-Interface: Programme verwalten — 2026-08-18

Erstes Listen-/Array-Datenmodell des Web-Interfaces. Design vorab per Artefakt abgestimmt, nach erstem Blick eine Detailkorrektur (Aktionsleiste blendet sich im Bearbeitungsmodus aus).

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Dateiauslieferung | `/programme.html`, `/programme.js` per `curl` geprüft | HTTP 200 | ✅ |
| 2 | Neues Programm anlegen | `main/programs/set` mit angehängtem Testprogramm (Array-Replace) | `main/programs/state` zeigt den neuen Eintrag | ✅ |
| 3 | Programm aktivieren | `main/program/cmd <neuer Index>` | `main/program/state` zeigt Name/Index korrekt, `activeProgram` in `main/programs/state` stimmt | ✅ |
| 4 | Programm löschen | `main/programs/set` ohne den Testeintrag, `activeProgram` explizit zurückgesetzt | Ausgangszustand exakt wiederhergestellt (JSON-Vergleich) | ✅ |
| 5 | Im Browser | Liste/Bearbeiten/Anlegen/Löschen/Aktivieren getestet | Funktioniert, ein Detail nachjustiert (siehe Spec) | ✅ nach Korrektur |

Vom Nutzer bestätigt: „sieht gut aus“.

## "MANUELL"-Konsistenz (Nachtrag zu Phase 13/14/17/18) — 2026-08-18

Firmware-Änderung: manuelle `time`/`auto`-Änderungen (`V{n}/time/set`, `V{n}/auto/set`, `main/config/set`) setzen `activeProgram` jetzt auf `0` zurück (`MqttManager::publishConfigStateAndClearProgram()`), Automatik-Toggle aus der Web-Konfigurationsseite entfernt, START-Button in beiden UIs gesperrt ohne gewähltes Programm. Auslöser/Design-Diskussion siehe `docs/spec/14-programme.md`, Nachtrag.

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Programm aktivieren | `main/program/cmd 1` | `activeProgram` in `main/programs/state` = `1` | ✅ |
| 2 | Manuelle Laufzeit-Änderung setzt zurück | `V1/time/set "9"` bei aktivem Programm | `activeProgram` = `0`, `main/program/state` = `{"index":0,"name":null}` | ✅ |
| 3 | Laufzeit trotzdem übernommen | s.o. | `V1/time/state` = `9` | ✅ |
| 4 | Alle Ventile auf Auto-OFF ("MANUELL" = alle grau) | `V1`–`V5` `auto/state` geprüft | Alle `OFF` | ✅ |
| 5 | Wiederherstellung | ursprüngliche Laufzeit + ursprüngliches Programm erneut gesetzt | Exakter Ausgangszustand (JSON-Vergleich) | ✅ |
| 6 | Web-Dashboard | Headline zeigt „Manueller Modus“, Ventilkacheln zeigen Laufzeit statt „nicht in Automatik“, START-Button grau/deaktiviert | Bestätigt im Browser | ✅ |
| 7 | Touch-Display | Programme-Button zeigt „Manueller Modus“ statt „Kein Programm“, START-Button gesperrt (kein Tap-Effekt) | Bestätigt am Gerät | ✅ |

Vom Nutzer bestätigt: „Ja, beide korrekt gesperrt“.

## Phase 20 — Web-Interface: Zeitplan verwalten — 2026-08-18

Komplexestes Datenmodell des Web-Interfaces (typabhängige Felder, Programm-Referenz per Name). Design vorab per Artefakt abgestimmt, nach erstem Blick eine Detailkorrektur (eigener Cleanup-Button statt Textlink).

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Dateiauslieferung | `/zeitplan.html`, `/zeitplan.js` per `curl` geprüft | HTTP 200 | ✅ |
| 2 | Neuer `daily`-Eintrag | `main/schedule/set` (Array-Replace) | `main/schedule/state` zeigt ihn korrekt | ✅ |
| 3 | `weekly`-Eintrag mit Wochentagen | `main/schedule/set` mit `weekdays` | Korrekt im State enthalten | ✅ |
| 4 | Globaler Schalter | `main/schedule/cmd OFF`/`ON` | `enabled` (global) wechselt entsprechend | ✅ |
| 5 | Cleanup abgelaufener Termine | abgelaufenen `once`-Eintrag gesetzt, `main/schedule/cleanup` gesendet | Nur der abgelaufene Eintrag entfernt | ✅ |
| 6 | Wiederherstellung | Ausgangszustand (leerer Zeitplan) erneut gesetzt | Exakt wiederhergestellt (JSON-Vergleich) | ✅ |
| 7 | Im Browser | Liste/Bearbeiten/Anlegen/Löschen/Cleanup/globaler Schalter getestet | Funktioniert, ein Detail nachjustiert (siehe Spec) | ✅ nach Korrektur |

## Dashboard-Neugestaltung: "Nächster Termin" + Programm-Picker im Hero — 2026-08-18

Rein browserseitige Erweiterung (`app.js`/`index.html`), keine Firmware-Änderung, daher primär im Browser statt per `paho-mqtt` geprüft.

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | "Nächster Termin"-Berechnung | Demo-Zeitplan gesetzt (`daily` 21:00, `weekly` Di/Fr 20:00) | Karte/Hero zeigt korrektes Programm + "Heute · 21:00" | ✅ |
| 2 | Programm-Picker öffnet/aktiviert | Klick auf Programmname im Hero, Auswahl aus Liste | Öffnet Dropdown, Auswahl aktiviert sofort per `main/program/cmd` | ✅ |
| 3 | Picker gesperrt während Automatik | Sequenz gestartet, Klick auf Picker | Kein Öffnen, `.disabled`-Zustand | ✅ |
| 4 | Fußbereich | Layout geprüft | Nur noch Diagnostics, volle Breite | ✅ |

Vom Nutzer bestätigt: „Passt alles“.

## Logging-Überarbeitung (Vorstufe zum Live-Log) — 2026-08-18

Firmware-Änderung, primär per Liveness-Check verifiziert (Fokus: keine Abstürze/Hänger durch die neuen Logging-Aufrufe in bislang stummen Modulen).

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Ventil manuell (`Source::VALVE`) | `V1/cmd ON`/`OFF` | Gerät reagiert normal, `V1/state` korrekt | ✅ |
| 2 | Konfiguration speichern (`ConfigStore`-Erfolgslogging) | `V1/time/set` | `V1/time/state` korrekt übernommen | ✅ |
| 3 | Programm anwenden | `main/program/cmd 1` | `main/program/state` korrekt | ✅ |
| 4 | Automatik start/stop (`Source::SEQ`) | `main/cmd ON`/`OFF` | `main/state` korrekt beide Richtungen | ✅ |
| 5 | Wiederherstellung | ursprüngliche Werte erneut gesetzt | Exakt wiederhergestellt | ✅ |

## Live-Log im Web-Interface — 2026-08-18

Mehrstufig entwickelt (Dashboard-Karte → eigene Seite → Tabelle mit Filtern), über viele Runden mit Nutzer-Screenshots verifiziert.

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Keine PUB/SUB-Leckage, keine Flut | Gemischte Aktionen (Ventil, Config, Programm, Automatik) 8s lang ausgelöst, `diagnostics/livelog` mitgeschnitten | 38 Zeilen, keine einzige `PUB`/`SUB`, keine Rückkopplung | ✅ |
| 2 | Boot-Puffer wird nachgeliefert | Echter Hardware-Reset (`esptool --after hard-reset`), `diagnostics/livelog` mitgeschnitten | I2C-Scan/WLAN/Setup-Zeilen korrekt in chronologischer Reihenfolge vor "MQTT Verbunden." | ✅ |
| 3 | Anfrage-Replay ohne frische Verbindungslücke | 5s Ruhephase, dann `diagnostics/livelog/replay` gesendet | Kompletter aktueller Puffer korrekt erhalten | ✅ |
| 4 | Web: eigene Log-Seite, Navigation | Seite geöffnet, Tab „Log“ geprüft | Eigenständige Seite, Tabellenansicht, Boot-Sequenz sichtbar | ✅ |
| 5 | Quelle-/Typ-Filter (Facetten-Prinzip) | I2C-Filter aktiviert | Nach Bugfix (Regex-Parsing, dann Ausschluss- statt Einschluss-Logik) korrekt nur I2C-Zeilen | ✅ nach 2 Korrekturen |
| 6 | Filter-Dropdown nicht abgeschnitten | Quelle-Dropdown bei reduzierter Zeilenzahl geöffnet | Nach Bugfix (`overflow:hidden` auf Karte, dann Kopf-/Körpertabelle getrennt) vollständig sichtbar | ✅ nach 2 Korrekturen |
| 7 | Event-Sucheingabe | Klick auf „Event“, Text eingegeben, Feld verlassen | Live-Filterung, Label „Eventfilter: *Begriff*“, Groß-/Kleinschreibung nach CSS-Fix (`text-transform`) korrekt erhalten | ✅ nach 2 Korrekturen |
| 8 | Lösch-Button, Enter-Verhalten | × geklickt, Enter im Suchfeld gedrückt | Filter zurückgesetzt bzw. Feld verlassen wie bei Klick daneben | ✅ |

Vom Nutzer abschließend bestätigt: „sehr geil, läuft jetzt perfekt!“.

## Backend-Logging-Review, PUB/SUB im Live-Log, PubSubClient-Reentrancy-Fix — 2026-08-18

Datei-für-Datei-Review deckte einen Bug auf: nach Freischalten der PUB/SUB-Durchleitung ins Live-Log folgte auf jede `SUB`-Zeile sporadisch eine falsche `MQTT ERROR Unbekanntes Topic: '<Datenmüll>'`-Zeile (PubSubClient-Pufferkonflikt durch synchrones `publish()` aus dem Empfangs-Callback). Fix: Live-Log-Publishes gepuffert, erst am Ende von `MqttManager::loop()` rausgeschickt.

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Bug reproduziert (vor Fix) | `test_livelog_pubsub.py`: Auto-Toggle, Programmwahl, Automatik-Start/-Stopp, 404-Aufruf | Jede `SUB`-Zeile gefolgt von falscher `Unbekanntes Topic`-ERROR-Zeile mit 2-Zeichen-Datenmüll (z. B. `'0X'`) | ❌ (vor Fix) |
| 2 | Fix verifiziert | Gleicher Testlauf nach Flash des gepufferten `flushPendingLogLines()`-Mechanismus | 108 Live-Log-Zeilen, keine einzige `Unbekanntes Topic`-Fehlzeile mehr | ✅ |
| 3 | PUB-Zeilen kommen durch | Gleicher Mitschnitt | 45 `PUB`-Zeilen vorhanden | ✅ |
| 4 | SUB-Zeilen kommen durch | Gleicher Mitschnitt | 6 `SUB`-Zeilen vorhanden | ✅ |
| 5 | Rausch-Filter weiterhin aktiv | Gleicher Mitschnitt, Automatik lief ca. 3s | 0 `remainingTotal`/`time/remaining`-Zeilen | ✅ |
| 6 | 404-Logging | `urllib`-Aufruf einer nicht existierenden Datei | `WEB DEBUG 404: /does-not-exist.txt`-Zeile vorhanden | ✅ |
| 7 | Kommandos weiterhin korrekt zugestellt | Programmwahl (`main/program/cmd`), Automatik-Start/-Stopp im selben Testlauf | Alle erwarteten Folge-PUBs (`V1/time/state`, `V1/auto/state`, `main/program/state`, `main/state`, …) korrekt und vollständig | ✅ |

## Backend-Logging-Review abgeschlossen: `applyProgram()`/MANUELL-Log, Display-Init-Fehlerlog — 2026-08-18

Letzte Runde der Datei-für-Datei-Review (`MqttManager.cpp`, `HmiManager.cpp`, `main.cpp`). Zwei neue Log-Zeilen in `MqttManager.cpp` mit `test_program_logging.py` verifiziert; `HmiManager::begin()`-Änderung (Display-Init-Fehlerprüfung) nur per Code-Review + erfolgreichem Boot nach dem Flash geprüft (kein gezielter Fehlerfall provoziert, da dafür Hardware manipuliert werden müsste).

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | "Programm angewendet"-Log | `main/program/cmd 1` gesendet, `diagnostics/livelog` mitgeschnitten | `MQTT INFO Programm 'Kurz' angewendet.` erscheint direkt nach den Auto-Flag-Resets, vor den State-Publishes | ✅ |
| 2 | MANUELL-Deselect-Log | Direkt danach `V1/time/set 3` (manuelle Änderung bei aktivem Programm) gesendet | `MQTT INFO MANUELL: Programm 'Kurz' durch direkte Aenderung abgewaehlt.` erscheint vor dem resultierenden Auto-Reset | ✅ |
| 3 | Keine Regression | Ausgangszustand (`V1/time` auf ursprünglichen Wert, Programm auf 0) danach wiederhergestellt, kompletter Mitschnitt (70 Zeilen) auf Auffälligkeiten geprüft | Alle erwarteten PUB/SUB/VALVE/SYS-Zeilen vorhanden, keine Fehlzeilen | ✅ |
| 4 | Build/Boot nach `HmiManager.cpp`-Änderung | `pio run --target upload`, Gerät bootet | Kein Build-Fehler, Display funktioniert nach dem Flash normal weiter (kein Fehlerfall aufgetreten, `gfx->begin()` liefert im Normalfall `true`) | ✅ |

## Frühere Phasen (2–13)

Einzeln je Phase manuell auf Hardware verifiziert, bevor die automatisierten Python/paho-mqtt-Skripte eingeführt wurden (ab Phase 14) — Details und Testfälle in den jeweiligen `docs/spec/*.md`-Dateien, kurz zusammengefasst in `docs/Log.md`. Kein struktureller Nacherfassungsbedarf, da der Regressionstest oben (Phase 12) dieselbe Funktionalität nochmal zusammenhängend abdeckt.
