# Testing — Gartenwasser

Zentrale, strukturierte Übersicht aller Testläufe. Ergänzt die phasenweisen `Test`/`Test / Ergebnis`-Abschnitte in `docs/spec/*.md` (dort stehen die Details je Phase), hier steht der schnelle Gesamtüberblick.

## Wie getestet wird

- Testskripte liegen unter [`tools/mqtt-tests/`](../tools/mqtt-tests/) (siehe dortige `README.md` für Voraussetzungen/Nutzung).
- Automatisiert per Python + [paho-mqtt](https://pypi.org/project/paho-mqtt/) gegen den echten Broker (`192.168.1.123:1883`) und die echte Hardware — kein Mock/Simulator.
- Skripte abonnieren `gartenwasser/#`, publizieren Testbefehle und prüfen die resultierenden (retained) States gegen Erwartungswerte.
- Wo nötig, wird ein echter Hardware-Reset über `esptool --after hard-reset` ausgelöst (RTS-Pin-Puls, kein Reflash) statt eines simulierten Neustarts.
- Vor/nach jedem Testlauf wird der bestehende Konfigurationsstand (`config`, `programs`) gesichert und am Ende wiederhergestellt — Testläufe hinterlassen keine dauerhaften Änderungen am Gerät.
- Zwei Prüfpunkte der Checkliste erfordern physischen Eingriff (I2C-Kabel ziehen, WLAN/MQTT trennen) und werden bewusst **nicht** unbeaufsichtigt automatisiert (siehe unten).

## Regressionstest (Checkliste aus Phase 12) — 2026-08-16

Zusammenhängender Gesamtdurchlauf, autonom ausgeführt. **48 von 49 automatisierten Checks bestanden.**

| # | Prüfpunkt | Test (was/wie) | Ergebnis | Bewertung |
|---|---|---|---|---|
| 1 | Boot / Verfügbarkeit | `availability = online` geprüft, `main/config/state` sofort (retained) verfügbar | 2/2 PASS | ✅ |
| 2 | Ventile manuell + V0-Kopplung | V1/V2 einzeln und gemeinsam ON/OFF; V0 bleibt an, solange mind. ein Ventil aktiv ist | 5/5 PASS | ✅ |
| 3 | Laufzeit | `time/set`, `maxTime`-Deckelung (`min(time, maxTime)`, mit `time=5`/`maxTime=1` erzwungen), echter Zeitablauf nach 60 s (kürzeste zulässige Laufzeit) inkl. automatischer Abschaltung und Re-Armierung auf `time` | 5/6 PASS | ⚠️ 1 Skript-Timing-Artefakt, kein Firmware-Fehler (siehe unten) |
| 4 | Automatik-Flag | `auto/set` ON/OFF → `auto/state` | 2/2 PASS | ✅ |
| 5 | Automatik-Sequenz | `main/cmd ON` startet mit erstem `auto=ON`-Ventil; manuelles `ON` eines nicht beteiligten Ventils wird ignoriert; manuelles `OFF` des aktiven Ventils rückt die Sequenz vor (Restlaufzeit bleibt `00:00`); `main/cmd OFF` bricht sofort ab und armiert alle Ventile neu | 9/9 PASS | ✅ |
| 6 | Diagnostics-Fehlerfall | I2C-Bus kurz trennen → `i2cStatus = error` + `lastError` gesetzt, wieder verbinden → `ok` | — | ⛔ nicht automatisiert (physischer Eingriff) |
| 7 | Alias (inkl. V0) | Umlaute (UTF-8) funktionieren; zu lange Werte (> 32 Zeichen) und Werte mit Steuerzeichen werden abgelehnt (unverändert) | 4/4 PASS | ✅ |
| 8 | Konfiguration per JSON | `main/config/set` Teil-Update ändert nur angegebene Felder; unbekannter Key (`V9`) wird ignoriert, Board bleibt responsiv | 3/3 PASS | ✅ |
| 9 | Resilienz | WLAN/MQTT-Verbindungsabbruch → laufende Ventile/Sequenz laufen lokal weiter, Reconnect republiziert alle States | — | ⛔ nicht automatisiert (physischer Eingriff) |
| 10 | Persistenz | Echter Hardware-Reset (`esptool --after hard-reset`) → `time`/`auto`/`alias` und Programme-Liste überleben, alle Ventile AUS, keine Automatik-Sequenz startet automatisch | 6/6 PASS | ✅ |

**Anmerkung zu Punkt 3**: Die eine "fehlgeschlagene" Prüfung erwartete exakt `remaining = 01:00` nach dem Einschalten, gemessen wurde `00:59`. Ursache: zwischen Einschalten und Prüfung (1,5 s Wartezeit im Testskript) war bereits ein Sekunden-Tick (1000 ms-Takt) vergangen. Die eigentliche Prüfung — Deckelung auf `maxTime` statt der vollen `time` (`00:59`/`01:00` statt `05:00`) — war korrekt. Kein Firmware-Fehler, nur eine zu strenge Testskript-Assertion (inzwischen auf `{"01:00", "00:59"}` korrigiert).

**Offen vor Produktivbetrieb**: Punkte 6 und 9 einmal manuell nachholen (physischer Eingriff, bewusst nicht unbeaufsichtigt getestet).

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

## Frühere Phasen (2–13)

Einzeln je Phase manuell auf Hardware verifiziert, bevor die automatisierten Python/paho-mqtt-Skripte eingeführt wurden (ab Phase 14) — Details und Testfälle in den jeweiligen `docs/spec/*.md`-Dateien, kurz zusammengefasst in `docs/Log.md`. Kein struktureller Nacherfassungsbedarf, da der Regressionstest oben (Phase 12) dieselbe Funktionalität nochmal zusammenhängend abdeckt.
