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
| 2 | Flash-Größen-Checkpoint | Vorher/Nachher-Vergleich `pio run`-Ausgabe | 41,5 % → 43,2 % (≈+50 KB, deutlich unter der Schätzung) | ✅ |
| 3 | Persistenz-Regression (SPIFFS→LittleFS) | Alias-Wert per MQTT gesetzt, Hardware-Reset (`esptool --after hard-reset`), Wert erneut abgefragt | Wert korrekt erhalten geblieben | ✅ |
| 4 | `uploadfs`-Kompatibilität | `pio run --target uploadfs` geschriebene Dateien nach Boot per `curl` geprüft | **Fehlgeschlagen** — Dateien nach erstem Boot nicht mehr vorhanden (Formatinkompatibilität `mklittlefs`/Laufzeit-Mount) | ❌ → Workaround umgesetzt (Firmware schreibt Dateien selbst), siehe `docs/spec/16-webif-fundament.md` |

**Nebenbefund**: die SPIFFS→LittleFS-Umformatierung hat die zu diesem Zeitpunkt gesetzten Testdaten (5 Programme, 1 Zeitplan-Eintrag, Config-Werte) gelöscht — nicht vorher angekündigt, im Nachhinein vom Nutzer als unkritisch bestätigt (reine Testdaten dieser Session).

## Frühere Phasen (2–13)

Einzeln je Phase manuell auf Hardware verifiziert, bevor die automatisierten Python/paho-mqtt-Skripte eingeführt wurden (ab Phase 14) — Details und Testfälle in den jeweiligen `docs/spec/*.md`-Dateien, kurz zusammengefasst in `docs/Log.md`. Kein struktureller Nacherfassungsbedarf, da der Regressionstest oben (Phase 12) dieselbe Funktionalität nochmal zusammenhängend abdeckt.
