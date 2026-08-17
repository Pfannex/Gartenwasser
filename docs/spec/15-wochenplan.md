# Phase 15 — Zeitplan / Scheduler (Tages- und Wochenplan)

**Status:** ✅ Erledigt & getestet (Kernfunktion) — Kollisions-Hinweis + „nächster Trigger"-Berechnung bewusst noch nicht umgesetzt (siehe unten)

## Ziel

Automatisch und autonom Bewässerungsprogramme (Phase 14) zu konfigurierten Zeitpunkten auslösen (`main/cmd ON`-Äquivalent) — ohne manuellen Eingriff. Tages- und Wochenplan sind **keine getrennten Features**, sondern beides Ausprägungen desselben, generischen Zeitplan-Mechanismus in einem eigenen `schedule`-Konfigurationsbereich (eigene Datei `/schedule.json`, eigenes Topic-Paar `main/schedule/set`/`main/schedule/state` — siehe `docs/requirements.md`, Abschnitt „Konfiguration“, Entscheidung 2026-08-16): eine Liste beliebig vieler Zeitplan-Einträge, jeder mit einer eigenen Trigger-Regel + einem referenzierten Programm (Phase 14).

Trigger-Regeln sollen mindestens abdecken:
- **Täglich, feste Uhrzeit** — z. B. „jeden Tag 21:00 Uhr".
- **Wöchentlich, fester Wochentag + Uhrzeit** — z. B. „jeden Dienstag" (+ Uhrzeit).
- **Einmalig, festes Datum + Uhrzeit** — z. B. „genau am 01.02.26, 11:00 Uhr".

Die Liste der Einträge ist beliebig erweiterbar (nicht auf 7 Tage/eine Uhrzeit pro Tag begrenzt) — z. B. mehrere Einträge am selben Tag, unterschiedliche Programme an unterschiedlichen Wochentagen, oder eine Mischung aus wiederkehrenden und einmaligen Terminen gleichzeitig.

Beispiel: Rasen jeden Tag 21:00 Uhr (täglicher Trigger, Programm „Rasen"), Beete jeden Dienstag und Freitag 20:00 Uhr (zwei wöchentliche Trigger, Programm „Beete").

## Voraussetzungen

- Phase 2 (NTP-Sync, Echtzeituhr) ✅ — Datum/Wochentag/Uhrzeit sind bereits verfügbar.
- Phase 7 (Automatik-Sequenz/Sequencer) — der Scheduler triggert im Kern dasselbe wie ein manuelles `main/cmd ON`.
- Phase 11 (`main/config/set`/`state`, JSON-Infrastruktur) — `schedule` folgt demselben Bulk-Set/State-Muster (eigene Datei/Topics statt Erweiterung von `config.json`, siehe Entscheidung 2026-08-16).
- Phase 14 (Bewässerungsprogramme) — ein Zeitplan-Eintrag wählt ein Programm aus, statt Zeiten/Auto-Flags erneut zu definieren.

## Design (erster Aufschlag, 2026-08-16 — noch nicht final, Details bei Umsetzung pruefen)

### JSON-Schema

```json
{
  "enabled": true,
  "schedule": [
    {"name": "Rasen abends", "enabled": true, "type": "daily", "time": "21:00", "program": "Rasen"},
    {"name": "Beete Di/Fr",  "enabled": true, "type": "weekly", "weekdays": ["tue", "fri"], "time": "20:00", "program": "Beete"},
    {"name": "Fruehjahrsstart", "enabled": true, "type": "once", "date": "2026-02-01", "time": "11:00", "program": "Kurz"}
  ]
}
```

- **`type`** unterscheidet `daily`/`weekly`/`once`; typ-spezifische Felder (`weekdays` nur bei `weekly`, `date` nur bei `once`).
- **`program`** referenziert ein Bewässerungsprogramm **per Name, nicht per Array-Index** (Entscheidung 2026-08-16) — sonst würde ein Umsortieren der Programme via `main/programs/set` die Zeitplan-Referenzen stillschweigend auf ein anderes Programm verschieben (dasselbe Problem, das die `shortcut`-Felder für `P1`–`P4` bereits lösen). `program` **muss nicht eindeutig sein** — dasselbe Programm darf in mehreren Einträgen referenziert werden (z. B. „Rasen" täglich abends **und** zusätzlich dienstags mit einem zweiten Eintrag). Verhalten bei nicht mehr existierendem Namen (Programm umbenannt/gelöscht) noch zu klären — vermutlich analog zu ungültigem `main/program/cmd`-Index: ignorieren + loggen.
- **`name`** je Eintrag ist rein kosmetisch (bessere Lesbarkeit z. B. in mqtt-spy), **nicht eindeutig**, **optional** und hat **keine Funktion** — insbesondere ersetzt es nicht `program`. Anders als bei den Programmen selbst (dort ist der Name faktisch der Identifikator) braucht kein Bestandteil des Zeitplan-Eintrags von außen eindeutig referenzierbar zu sein, da nichts einzeln auf einen Eintrag zeigt — die ganze Liste wird immer als Block über `main/schedule/set` ersetzt (kein „ändere nur Eintrag Nr. 3"). Eine Eindeutigkeits-/ID-Pflicht (weder für `name` noch als zusätzliches `index`-Feld) wurde deshalb bewusst **nicht** eingeführt (Entscheidung 2026-08-16).
- **`enabled`** je Eintrag: einzelne Einträge pausieren, ohne sie zu löschen.
- **`enabled`** auf oberster Ebene (2026-08-16 bestätigt): globaler Ein/Aus-Schalter für den kompletten Zeitplan — bei `false` ist der Zeitplan komplett außer Betrieb (kein Eintrag löst aus), ohne dass die Konfiguration verloren geht (z. B. „Urlaubsmodus“). Zusätzlich zum Bulk-Feld ein schlankes Convenience-Topic `main/schedule/cmd` (`ON`/`OFF`), analog zu `main/program/cmd` als Singular-Pendant zu `main/programs/set` — praktisch für ein späteres Home-Assistant-Switch-Entity (Phase 10), ohne JSON senden zu müssen.
- Einmal pro Minute prüfen, ob „jetzt" einem aktiven (`enabled`) Trigger entspricht → referenziertes Programm auswählen + Sequenz starten (derselbe Pfad wie `main/cmd ON`, inkl. der seit Phase 14 geltenden Regel „Automatik erfordert Programm“ — der Scheduler hat ja durch die Referenz immer eins).

### Feldreferenz

| Feld | Ebene | Pflicht | Werte/Format | Bedeutung |
|---|---|---|---|---|
| `enabled` | oberste Ebene | optional (Default `true`) | `true` \| `false` | Globaler Ein/Aus-Schalter für den kompletten Zeitplan. `false` = kein Eintrag löst aus, Konfiguration bleibt erhalten. |
| `schedule` | oberste Ebene | ja | Array | Liste der Zeitplan-Einträge, beliebig lang (keine feste Obergrenze wie bei Programmen vorgesehen — bei Bedarf bei der Umsetzung ergänzen). |
| `name` | je Eintrag | optional | Freitext, nicht eindeutig | Rein kosmetische Beschriftung, keine Funktion. |
| `enabled` | je Eintrag | optional (Default `true`) | `true` \| `false` | Einzelnen Eintrag pausieren, ohne ihn zu löschen. |
| `type` | je Eintrag | ja | `"daily"` \| `"weekly"` \| `"once"` | Trigger-Art, bestimmt welche der folgenden Felder zusätzlich gelten. |
| `time` | je Eintrag | ja | `"HH:MM"` (24h, führende Nullen, z. B. `"09:05"`) | Uhrzeit des Triggers. |
| `weekdays` | je Eintrag, nur bei `type: "weekly"` | ja (bei `weekly`) | Array aus `"mon"`, `"tue"`, `"wed"`, `"thu"`, `"fri"`, `"sat"`, `"sun"` (englische Kurzform, konsistent zu den übrigen Enum-Werten im Projekt) | Wochentag(e), an denen der Trigger feuert — mehrere gleichzeitig möglich. |
| `date` | je Eintrag, nur bei `type: "once"` | ja (bei `once`) | `"YYYY-MM-DD"` (ISO 8601) | Einmaliges Datum. |
| `program` | je Eintrag | ja | String, muss exakt einem vorhandenen Programmnamen entsprechen | Welches Programm angewendet wird. Darf sich über mehrere Einträge wiederholen (keine Eindeutigkeitspflicht). |

### Weitere Beispiele

**Dasselbe Programm mehrfach terminiert** (zeigt, warum `program` keine Eindeutigkeitspflicht hat):

```json
{"name": "Rasen Standard", "type": "daily", "time": "21:00", "program": "Rasen"},
{"name": "Rasen Zusatz heiss", "type": "weekly", "weekdays": ["tue"], "time": "06:00", "program": "Rasen"}
```

**Pausierter Einzeleintrag** (`enabled: false`, Konfiguration bleibt erhalten, feuert aber nicht):

```json
{"name": "Beete Winterpause", "enabled": false, "type": "weekly", "weekdays": ["tue", "fri"], "time": "20:00", "program": "Beete"}
```

**Minimaler Eintrag ohne `name`** (`name` ist optional):

```json
{"type": "daily", "time": "07:30", "program": "Kurz"}
```

**Kompletter Zeitplan pausiert** (globaler Schalter, z. B. Urlaub — einzelne Einträge bleiben wie konfiguriert):

```json
{"enabled": false, "schedule": [{"name": "Rasen abends", "type": "daily", "time": "21:00", "program": "Rasen"}]}
```

**Mehrere Wochentage in einem Eintrag** (statt mehrerer separater `weekly`-Einträge):

```json
{"name": "Beete Mo/Mi/Fr", "type": "weekly", "weekdays": ["mon", "wed", "fri"], "time": "19:30", "program": "Beete"}
```

### Entschiedene Punkte

1. **Programm-Referenz per Name** (siehe oben) statt Array-Index.
2. **Verpasster Trigger (Reboot/Stromausfall im Startfenster) → verfällt, wird nicht nachgeholt.** Begründung (Nutzer, 2026-08-16): sonst würde automatisch etwas zu einem Zeitpunkt passieren, den man nicht explizit gewünscht hat — ein nachgeholter Trigger zu einer unvorhersehbaren Zeit (z. B. erst beim nächsten Boot Stunden später) wäre überraschender/unerwünschter als ein einmalig ausgefallener Termin.
3. **Globaler Ein/Aus-Schalter** (`enabled` + `main/schedule/cmd`) — siehe oben.
4. **Mechanik: minütlicher Prüfloop über die komplette Liste** (2026-08-16 abgestimmt) — kein Timer pro Eintrag, ein periodischer Check analog zum bestehenden sekündlichen Ventil-Tick in `MqttManager::loop()` (`millis()`-Intervall-Gating), nur auf Minutenebene:
   - Der Scheduler merkt sich die zuletzt geprüfte Minute (`lastCheckedMinute`, aus der NTP-synchronisierten Echtzeituhr, siehe Phase 2). Bei jedem `loop()`-Durchlauf: hat sich die aktuelle Minute geändert? Falls ja, einmal die **gesamte** `schedule`-Liste durchgehen, `lastCheckedMinute` aktualisieren. So wird jede Minute garantiert genau einmal ausgewertet — kein Doppel-Feuern, keine Lücke.
   - Pro Eintrag mit `enabled: true` (und globalem `enabled: true`) prüfen, ob „jetzt" passt: `daily` → `time` == aktuelle HH:MM; `weekly` → zusätzlich heutiger Wochentag in `weekdays`; `once` → zusätzlich `date` == heutiges Datum.
   - Match → `program` per Name nachschlagen (neue `ConfigStore`-Funktion nötig, bisher gibt es nur die Suche per `shortcut`), auswählen, dann derselbe Startpfad wie `main/cmd ON` (`startSequence()`) — inklusive der bestehenden Regel „Automatik erfordert Programm" (trivial erfüllt) und des Reapply-Verhaltens (Phase 14).
   - **Löst nebenbei die „gleichzeitige Trigger"-Frage ohne neue Logik**: `startSequence()` prüft bereits `Sequencer::isRunning()` zuerst — feuern zwei Einträge in derselben Minute, startet der erste in Array-Reihenfolge, der zweite trifft beim Verarbeiten in derselben Schleife auf eine bereits laufende Sequenz und wird vom bestehenden Guard abgewiesen (geloggt wie gehabt). „Erster in Listen-Reihenfolge gewinnt" ergibt sich automatisch.
   - Der Kollisions-**Hinweis** (Merker unten) ist davon strikt zu unterscheiden: eine **separate**, aufwendigere Funktion „nächster fälliger Trigger über alle aktiven Einträge" (echte Wiederkehr-Mathematik für `daily`/`weekly`, nicht nur der einfache Minuten-Abgleich).

### Neue Punkte (Nutzer-Merker, 2026-08-16, noch zu verfeinern)

- **Kollisions-Hinweis bei manuellem `main/cmd ON`** (Touch **und** MQTT): läuft eine manuell gestartete Sequenz voraussichtlich noch, wenn der nächste Zeitplan-Trigger fällig wird (`main/remainingTotal` würde über den nächsten geplanten Start hinausreichen), soll ein Hinweis erfolgen — nicht blockierend, nur Information (passt zum bisherigen Stil: Hinweis statt Verhinderung, siehe `docs/spec/13-touch-ui.md`). Braucht die oben beschriebene separate „nächster Trigger"-Berechnung. Anzeige vermutlich Touch-Statuszeile (transienter Hinweis) + `lastError`/Log für den MQTT-Fall — Details bei Umsetzung.
- **Aufräum-Funktion für abgelaufene `once`-Einträge** — **umgesetzt** als `main/schedule/cleanup` (beliebiger Payload, reiner Einmalbefehl): entfernt alle Einträge, die nie wieder auslösen können (abgelaufene `once`-Termine, lexikographischer Datumsvergleich Jahr/Monat/Tag). Kein Automatismus — bewusst nur auf Anfrage, damit „warum ist mein Eintrag weg“ nicht überrascht. Ergänzt (löst aber nicht ab) das oben entschiedene „liegen lassen“-Verhalten.

### Verworfen

- **Eigenes Fehler-Topic `main/schedule/settingsError`** (2026-08-16 verworfen): ursprünglich für kollidierende Trigger und allgemeine Konfigurationsfehler angedacht. Da überschneidende Einträge bewusst erlaubt sind und über „erster in Listen-Reihenfolge gewinnt" automatisch aufgelöst werden (siehe Mechanik oben) — kein Fehlerfall, nichts zu melden. Allgemeine Konfigurationsfehler (z. B. ungültiger `program`-Name) laufen stattdessen über den bereits bestehenden generischen Kanal `diagnostics/lastError`, wie überall sonst im Projekt (kein neues Topic nötig).

### Noch offene Fragen

- Verhältnis zu manuellem `main/cmd ON`/`OFF` während eines geplant ausgelösten Laufs — verhält sich bereits identisch zur bestehenden Regel „manuelles Aus wird angenommen, Sequenz macht weiter" (keine Sonderbehandlung nötig, der Scheduler startet nur dieselbe Sequenz wie `main/cmd ON`). Der Kollisions-Hinweis bei einem **neuen** manuellen Start waehrend ein Zeitplan-Trigger bald faellig ist, bleibt offen (siehe Merker oben).
- Genaue Berechnung/Implementierung der „nächster fälliger Trigger"-Funktion (Wiederkehr-Mathematik für `daily`/`weekly`) — noch nicht umgesetzt, siehe „Umsetzung" unten.

## Umsetzung (2026-08-16)

- `ConfigStore`: neue Datei `/schedule.json`, `kMaxScheduleEntries = 16`, `kScheduleJsonCapacity = 4096` Byte. `ScheduleType`-Enum (`DAILY`/`WEEKLY`/`ONCE`), `ScheduleInput`-Struct fuer `setSchedule()` (Bulk-Replace, analog `setPrograms()`), Getter je Eintrag (0-basierter Index, kein von aussen ansprechbarer Identifikator noetig). Neue `getProgramIndexForName()` fuer die Programm-Referenz per Name (ergaenzt die bestehende `getProgramIndexForShortcut()`). Zeit („HH:MM") und Datum ("YYYY-MM-DD") werden beim Laden/Serialisieren geparst/formatiert — Ausgabe ueber Arduino `String` statt lokaler `char`-Puffer, um denselben Dangling-Pointer-Fehler wie bei den `shortcut`-Strings in Phase 14 zu vermeiden (ArduinoJson kopiert `String`-Inhalte in den Dokument-Pool, bei rohem `const char*` auf einen Stack-Puffer waere das nicht der Fall).
- `MqttManager`: `main/schedule/set` (Bulk, Array-Replace + globales `enabled`), `main/schedule/state` (retained), `main/schedule/cmd` (`ON`/`OFF`, globaler Schalter), `main/schedule/cleanup` (Einmalbefehl). Ungueltige Einzeleintraege (fehlendes `program`, unbekannter `type`, ungueltige `time`/`date`, `weekly` ohne gueltige `weekdays`) werden einzeln uebersprungen + geloggt, nicht die ganze Anfrage abgelehnt (wie ueberall sonst im Projekt).
- **Scheduler-Tick** (`checkSchedule()`): laeuft unconditional in `MqttManager::loop()` neben `tickValveTimers()`/`checkDiagnostics()` (unabhaengig von WLAN/MQTT, siehe Kernentscheidung 4). Deaktiviert sich selbst, solange `Logger::isRealTimeEnabled()` false ist (keine verlaessliche Uhrzeit vor NTP-Sync) — dafuer neue `Logger::isRealTimeEnabled()`-Abfrage ergaenzt. Bei Trigger-Match: `applyProgram()` (per Name aufgeloest) + `startSequence()`, exakt derselbe Pfad wie `main/cmd ON`.
- **Stack-Konsequenz**: `kScheduleJsonCapacity` (4096 Byte) ist groesser als `kProgramsJsonCapacity` (2048 Byte) und wird jetzt der puffer-bestimmende Fall (`kMaxJsonPayloadSize` in `MqttManager.cpp`, `mqttClient.setBufferSize()`). `SET_LOOP_TASK_STACK_SIZE` in `main.cpp` vorsorglich von 16 KB auf 32 KB verdoppelt (RAM-Headroom reichlich vorhanden), um denselben Stack-Overflow-Bug wie bei Phase 14 von vornherein zu vermeiden.
- **Noch nicht umgesetzt** (bewusst zurückgestellt): Kollisions-Hinweis bei manuellem `main/cmd ON` nahe am naechsten Trigger, sowie die dafuer noetige „naechster faelliger Trigger"-Berechnung. Ebenso: Touch-UI-Anzeige/-Bedienung fuer den Zeitplan (siehe Backlog-Idee in `docs/spec/13-touch-ui.md`).

## Betroffene Dateien

- `src/ConfigStore.h/.cpp` (`/schedule.json`, `ScheduleType`/`ScheduleInput`, Getter, `getProgramIndexForName()`)
- `src/MqttManager.h/.cpp` (Subscribe-Handler `main/schedule/set`/`cmd`/`cleanup`, Publish `main/schedule/state`, `checkSchedule()`)
- `src/Logger.h/.cpp` (neue `isRealTimeEnabled()`-Abfrage)
- `src/main.cpp` (`SET_LOOP_TASK_STACK_SIZE` erhoeht)

## MQTT-Topics

- `gartenwasser/main/schedule/set` (subscribe, JSON, `"schedule"` ersetzt das komplette Array, `"enabled"` setzt den globalen Schalter, beide optional, nicht retained)
- `gartenwasser/main/schedule/state` (publish, retained, JSON, kompletter Gesamtstand)
- `gartenwasser/main/schedule/cmd` (subscribe, `ON`/`OFF`, globaler Schalter, nicht retained)
- `gartenwasser/main/schedule/cleanup` (subscribe, beliebiger Payload, entfernt abgelaufene `once`-Eintraege, nicht retained)

## Test

1. `main/schedule/set` mit je einem `daily`/`weekly`/`once`-Eintrag → `main/schedule/state` zeigt alle drei korrekt (inkl. typ-spezifischer Felder).
2. `main/schedule/cmd OFF`/`ON` → `enabled` in `main/schedule/state` wechselt entsprechend.
3. Ungueltige Eintraege (fehlendes `program`, unbekannter `type`, ungueltige `time`, unbekannter Wochentag) gemischt mit einem gueltigen Eintrag senden → nur der gueltige bleibt uebrig, `diagnostics/lastError` zeigt die Ablehnung.
4. `main/schedule/cleanup` mit einem abgelaufenen und einem zukuenftigen `once`-Eintrag plus einem `daily`-Eintrag → nur der abgelaufene wird entfernt.
5. Echter Trigger-Test: `daily`-Eintrag auf eine Uhrzeit wenige Minuten in der Zukunft gesetzt, gewartet → Sequenz startet automatisch zur exakten Minute (`main/state`, `activeValve`, `main/program/state` korrekt).

## Test / Ergebnis

Automatisiert per Python/paho-mqtt gegen den echten Broker getestet (Skript nicht dauerhaft im Repo, ad hoc erstellt) — alle 5 Testfaelle oben bestanden, inkl. eines echten Zeit-Tests (Trigger 2 Minuten in der Zukunft gesetzt, feuerte exakt zur Minute: Programm „Kurz" automatisch angewendet, Sequenz gestartet). Testzustand danach vollstaendig zurueckgesetzt (Sequenz gestoppt, Test-Zeitplan geleert, Programmwahl zurueckgesetzt).

## Zweck des Eintrags

Bewusst früh im Backlog dokumentiert, damit Entscheidungen in den vorherigen Phasen (v. a. Sequencer/Phase 7, Konfiguration/Phase 11, Programme/Phase 14) nicht in eine Richtung laufen, die einen späteren, generischen Scheduler erschwert.
