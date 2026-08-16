# Funktionsbeschreibung Gartenbewässerung

## Übersicht

| | |
|---|---|
| Name | Gartenbewässerung |
| Version | V1.0 |
| Hersteller | Pf@nne |
| ESP | ESP32-C6FH8-8MB-172×320-SPI-JD9853-AXS5106L |

## Hardware

Ansteuerung von 6 Bewässerungsventilen: `V0` = Hauptventil, `V1`–`V5` = Bewässerungsventile.
Die Ansteuerung erfolgt über I2C-Bus auf einen MCP23017-Portexpander (Adresse `0x20`).

### MCP23017-Pinbelegung (Port B)

| Ventil | Pin |
|---|---|
| V0 (Hauptventil) | B7 |
| V1 | B2 |
| V2 | B3 |
| V3 | B4 |
| V4 | B5 |
| V5 | B6 |

## Software-Funktionalität
### Home Assistant 

Das Device soll vollständig automatisch in Home Assistant per MQTT Autodetect implementiert werden können. 
Die entsprechenden MQTT-Topics und Verhaltensweisen sind zu implementieren. 

### Code Anforderungen 

- `V1`–`V5` sind einzeln ON/OFF steuerbar, mit Status ON/OFF.
- Zum Bewässern mit `V1`–`V5` muss immer `V0` mit eingeschaltet sein. Die Einschaltung von `V0` erfolgt gemeinsam mit `V1`–`V5`.
- `V1`–`V5` haben je eine einstellbare Einschaltdauer in Minuten (`time`) für die Bewässerungszeit.
- Die Restlaufzeit (`timeLeft`) nach Einschaltung wird im Format `mm:ss` im Sekundentakt bereitgestellt.
- `V1`–`V5` haben je ein Flag zur Aktivierung der Automatik (`AUTO` ON/OFF).
- Die Ausschaltung der Ventile muss zu jedem Zeitpunkt möglich sein.
- Mehrere Ventile `V1`–`V5` dürfen gleichzeitig manuell geöffnet sein (Wasserdruck-Verantwortung liegt bei der Installation).
- Ein WLAN-/MQTT-Verbindungsabbruch unterbricht laufende Ventile oder eine laufende Automatik-Sequenz **nicht** — beides läuft lokal/autonom auf dem ESP32 weiter, da dieser die Zeiten selbst misst. Nach Wiederverbindung wird der aktuelle Status einfach neu publiziert.
- Nach jedem Boot (Neustart/Stromausfall) sind alle Ventile grundsätzlich AUS, eine Automatik-Sequenz startet nicht automatisch neu (sicherer Grundzustand).

### Automatisierungsfunktionen

- Eine übergeordnete Start/Stop-Funktion (`ON`/`OFF`) steuert eine Ablaufsequenz, die nacheinander die Ventile `V1`–`V5` ansteuert und nach `time` wieder ausschaltet.
- **Voraussetzung für `ON` (seit Phase 14, 2026-08-16)**: es muss ein Bewässerungsprogramm gewählt sein (`activeProgram != 0`, siehe „Bewässerungsprogramme“ unten) — ohne Programm bleibt `ON` wirkungslos (weder Touch noch MQTT), da eine Ablaufsequenz ohne definierten Rahmen konzeptionell kein sinnvolles Ziel mehr hat, seit es Programme gibt. Direktes Ventilschalten (`V{n}/cmd`) ist davon **nicht** betroffen und bleibt jederzeit uneingeschränkt möglich (das ist der eigentliche „manuelle“ Weg). Beim Start wird das gewählte Programm zusätzlich noch einmal frisch angewendet, damit garantiert dessen Werte laufen und nicht zwischenzeitlich manuell abgewichene Flags.
- Die übergeordnete Start/Stop-Funktion (`OFF`) schaltet zu jedem Zeitpunkt alle Ventile aus und setzt alle Timer auf `time` zurück.
- Nach dem automatischen Durchlauf müssen alle Ventile aus sein und alle Timer wieder auf `time` zurückgesetzt sein.
- Das aktive Ventil der Sequenz wird angezeigt.
- Manuelles Einschalten (`V{n}/cmd ON`) wird während einer laufenden Automatik-Sequenz ignoriert.
- Manuelles Ausschalten (`V{n}/cmd OFF`) des gerade aktiven Ventils wird während der Automatik **angenommen** — die Sequenz fährt danach normal mit dem nächsten Ventil fort (kein Abbruch der Gesamtsequenz).
- `maxTime` (siehe MQTT-Topic-Struktur, `main/time/maxTime`) ist eine harte Obergrenze **pro Ventil**: effektive Laufzeit = `min(time, maxTime)`, gilt sowohl manuell als auch automatisch. Ein Erreichen von `maxTime` wird technisch identisch zu einem normalen Zeitablauf behandelt (Ventil aus, Automatik fährt mit dem nächsten Ventil fort). Eine eigene Überwachung der Gesamtsequenz-Laufzeit ist dadurch nicht nötig — sie ist implizit auf maximal `5 × maxTime` begrenzt.

### Sicherheit

- Die Magnetventile öffnen nur unter Spannung (stromlos geschlossen). Ein I2C-Bus-Ausfall oder Stromausfall führt dadurch automatisch zum sicheren Zustand (alle Ventile zu).

### Persistenz

- Alle Einstellwerte (`time`, `auto`, `alias`, `maxTime`) werden dauerhaft im Flash-Dateisystem (SPIFFS) gespeichert und überleben einen Neustart.

### Konfiguration (Backup/Restore, Presets)

Die Konfiguration ist auf drei unabhängige Bereiche aufgeteilt — eigene SPIFFS-Datei und eigenes MQTT-Topic-Paar je Bereich. Grund: die drei wachsen unterschiedlich schnell und ändern sich aus unterschiedlichen Gründen (laufende Ventilparameter selten und einzeln, Programme gelegentlich als Ganzes, Zeitplan perspektivisch am umfangreichsten) — eine gemeinsame Struktur würde mit jeder weiteren Phase unübersichtlicher und der MQTT-/JSON-Puffer müsste immer weiter wachsen, obwohl die meisten Änderungen nur einen der drei Bereiche betreffen.

| Bereich | Datei | Inhalt | Topics |
|---|---|---|---|
| `config` | `/config.json` | `time`, `auto`, `alias`, `maxTime` | `main/config/set` / `main/config/state` |
| `programs` | `/programs.json` | Bewässerungsprogramme (Phase 14, geplant) | `main/programs/set` / `main/programs/state` (Bulk), `main/program/cmd` / `main/program/state` (Auswahl per Index) |
| `schedule` | `/schedule.json` | Zeitplan (Phase 15, geplant) | `main/schedule/set` / `main/schedule/state` |

Jeder Bereich funktioniert nach demselben Muster: `.../set` (JSON, **nicht** retained) setzt den Bereich komplett oder teilweise — fehlende Keys bleiben unverändert (kein Rücksprung auf Defaults, anders als beim Laden einer fehlenden Datei nach Boot). `.../state` (JSON, retained) publiziert den aktuellen Gesamtstand des jeweiligen Bereichs, bei jeder Änderung und nach jedem (Re-)Connect. Validierung wie bei den einzelnen `V{n}/...`-Topics (ungültige Werte ignorieren + loggen statt übernehmen). Bewusst nicht retained (die `set`-Topics): ein Broker-Neustart oder erneutes Subscriben darf keine alte Konfiguration versehentlich erneut anwenden.

Ausnahme vom Teil-Update-Prinzip: `programs` (Array) hat keine natürlichen Schlüssel für einen Feld-Merge — der Key `"programs"` in `main/programs/set` ersetzt das komplette Array, wenn mitgeschickt. Gleiches gilt später für `schedule`. Praktischer Ablauf: `.../state` holen, lokal bearbeiten, komplett zurückschicken.

Zweck: schneller, reproduzierbarer Ausgangszustand für Tests und im laufenden Betrieb, ohne fest im Firmware-Code hinterlegte Presets — „Default“- oder „Test“-Konfigurationen sind einfach extern gespeicherte JSON-Payloads, die bei Bedarf auf das jeweilige `.../set`-Topic publiziert werden.

Ersetzt die ursprünglich für Phase 11 vorgesehenen Sammel-Befehle `main/time/set`/`main/auto/set` (JSON) — deckt deren Funktion vollständig ab, ohne pro Domäne ein eigenes Topic zu brauchen.

**Vollständiges Beispiel `config`** (`main/config/state`, alle Elemente — ein `set` kann jede beliebige Teilmenge davon enthalten):

```json
{
  "time": {"V1": 5, "V2": 10, "V3": 5, "V4": 15, "V5": 5},
  "auto": {"V1": true, "V2": true, "V3": false, "V4": true, "V5": false},
  "alias": {
    "V0": "Hauptventil",
    "V1": "Rasen Vorgarten",
    "V2": "Rasen Garten",
    "V3": "Beet Rosen",
    "V4": "Beet Gemüse",
    "V5": "Kübelpflanzen"
  },
  "maxTime": 30
}
```

`alias` enthält zusätzlich `V0` (Hauptventil); `time`/`auto` gibt es nur für `V1`–`V5`. Details siehe `docs/spec/11-sammelbefehle.md`.

**Vollständiges Beispiel `programs`** (`main/programs/state`):

```json
{
  "programs": [
    {"name": "Kurz",  "shortcut": "P1", "time": {"V1": 2, "V2": 2}, "auto": {"V1": true, "V2": true, "V3": false, "V4": false, "V5": false}},
    {"name": "Rasen", "shortcut": "P2", "time": {"V1": 10, "V2": 10}, "auto": {"V1": true, "V2": true, "V3": false, "V4": false, "V5": false}},
    {"name": "Alles", "shortcut": "P3", "time": {"V1": 8, "V2": 8, "V3": 12, "V4": 15, "V5": 6}, "auto": {"V1": true, "V2": true, "V3": true, "V4": true, "V5": true}},
    {"name": "Test",  "time": {"V1": 1, "V2": 1, "V3": 1, "V4": 1, "V5": 1}, "auto": {"V1": true, "V2": true, "V3": true, "V4": true, "V5": true}}
  ],
  "activeProgram": 2
}
```

`time`/`auto` je Programm sind Teilmengen — was drinsteht, wird beim Anwenden übernommen, was fehlt, bleibt unverändert (dieselbe Semantik wie bei `main/config/set`). `activeProgram` ist 1-basiert (`0` = kein Programm gewählt), und lebt bewusst hier (nicht in `config`), weil er sich auf die Programme bezieht. `shortcut` (optional, `"P1"`–`"P4"`) bindet ein Programm an einen Touch-UI-Button, unabhängig von seiner Position im Array — fehlt der Key (wie bei „Test“ oben), ist das Programm nur per `main/program/cmd`/`main/programs/set` erreichbar. Doppelt vergebene Shortcuts werden nicht abgelehnt (würde dem Array-Replace-Prinzip widersprechen), sondern beim Auflösen löst der erste Treffer in Array-Reihenfolge, zusätzlich mit einem Log-Hinweis. `maxTime` und `alias` sind bewusst kein Teil eines Programms. Die schlanke Einzelwert-Auswahl `main/program/cmd <n>` (Singular) bleibt zusätzlich bestehen — bequemer Weg z. B. für die Touch-UI-Buttons, ohne JSON senden zu müssen; intern dieselbe Wirkung wie `activeProgram` über `main/programs/set` zu setzen. Details siehe `docs/spec/14-programme.md`.

**`schedule`** (`main/schedule/state`, Phase 15, geplant, Schema noch offen): Array von Zeitplan-Einträgen (Trigger-Typ `daily`/`weekly`/`once` + Programm-Referenz). Details siehe `docs/spec/15-wochenplan.md`.

### Touch-UI (Phase 13/14, fertig)

- Toggle-Button „AUTO“/„OFF“ auf dem Display, gekoppelt an `main/cmd` (startet nur mit gewähltem Programm, siehe „Automatisierungsfunktionen“ oben).
- Ventile `V0`–`V5` als LED-Statusindikatoren (grün/rot/dunkelgrau bei `auto=OFF` + AUS), aktives Sequenz-Ventil gelb hervorgehoben.
- Statuszeile (Fußleiste) zeigt priorisiert, was gerade passiert: Fehler > transienter Hinweis (2s, z. B. „Kein Programm vorgewählt!“) > laufende Automatik > manueller Betrieb > gewähltes Programm (Name) > „MANUELL“ (kein Programm gewählt).
- 4 Buttons „P1“–„P4“ wenden das Programm mit passendem `shortcut`-Feld an (siehe „Bewässerungsprogramme“ oben); erneuter Druck auf ein bereits aktives Programm wählt es wieder ab (Toggle). Details siehe `docs/spec/13-touch-ui.md`.

## MQTT-Topic-Struktur

```
TOPIC                  | RETAIN | VALUE              | DESCRIPTION
------------------------------------------------------------------
gartenwasser/          |        |                    |
├── availability       | ja     | online|offline     | LWT
├── V0/                |        |                    |
│   ├── state          | ja     | ON|OFF             | read-only
│   ├── alias          | ja     | "Hauptventil"      | Klartextname
│   └── alias/set      | nein   | "Text"             | Editieren des Alias-Namens
├── V1/ .. V5/         |        |                    |
│   ├── state          | ja     | ON|OFF             | read-only, Ist-Zustand
│   ├── cmd            | nein   | ON|OFF             | Befehl, von HA
│   ├── alias          | ja     | "Rasen Seite"      | Klartextname
│   │   └── set        | nein   | "Text"             | Editieren des Alias-Namens
│   ├── time/          |        |                    |
│   │   ├── state      | ja     | <Minuten>          | aktuell eingestellte Laufzeit
│   │   ├── set        | nein   | <Minuten>          | Befehl, von HA
│   │   └── remaining  | nein   | mm:ss              | Restlaufzeit, Sekundentakt
│   └── auto/          |        |                    |
│       ├── state      | ja     | ON|OFF             | Automatik-Flag Ist
│       └── set        | nein   | ON|OFF             | Automatik-Flag Befehl
├── main/              |        |                    |
│   ├── cmd            | nein   | ON|OFF             | Start/Stop Befehl
│   ├── state          | ja     | ON|OFF             | Sequenz läuft?
│   ├── activeValve    | ja     | "V1".."V5"|"-"     | aktives Ventil
│   ├── remainingTotal | nein   | mm:ss              | Restzeit der gesamten Sequenz
│   ├── time/          |        |                    |
│   │   └── maxTime    | ja     | <Minuten>          | Obergrenze pro Ventil, effektive Laufzeit = min(time, maxTime)
│   ├── config/        |        |                    |
│   │   ├── set        | nein   | JSON               | time/auto/alias/maxTime setzen (komplett oder teilweise)
│   │   └── state      | ja     | JSON               | Aktueller Gesamtstand von config (retained, Backup/Restore, Presets)
│   ├── programs/      |        |                    |
│   │   ├── set        | nein   | JSON               | Programme-Array + activeProgram setzen (Array wird komplett ersetzt)
│   │   └── state      | ja     | JSON               | Aktueller Gesamtstand von programs (retained)
│   ├── program/       |        |                    |
│   │   ├── cmd        | nein   | <integer>          | Programm per Index auswählen, 1-basiert (0 = keins)
│   │   └── state      | ja     | JSON               | {"index":n,"name":"..."} , aktuell gewähltes Programm
│   └── schedule/      |        |                    | Phase 15, geplant
│       ├── set        | nein   | JSON               | Zeitplan-Array setzen (komplett ersetzt)
│       └── state      | ja     | JSON               | Aktueller Zeitplan (retained)
└── diagnostics/       |        |                    |
    ├── i2cStatus      | ja     | ok|error           | Status i2cBus / MCP23017
    └── lastError      | ja     | <Text/Zeitstempel> | letzte Fehlermeldung
```

## Zugangsdaten

WLAN- und MQTT-Zugangsdaten liegen ausschließlich in `include/secrets.h` (nicht versioniert, siehe `.gitignore`). Vorlage: `include/secrets.h.example`.

| Parameter | Wert |
|---|---|
| MQTT-Broker | `192.168.1.123:1883` |
| MQTT Client-ID | `GardenWater` |

## Architektur (Firmware)

Die Firmware ist in eigenständige Klassen mit jeweils eigener `.h`/`.cpp`-Datei gegliedert (`src/`):

| Klasse | Zuständigkeit | Status |
|---|---|---|
| `Logger` | Einheitliches Log-Format für alle Subsysteme | ✅ Fertig |
| `WifiManager` | WLAN-Verbindung, Reconnect, NTP-Sync beim Boot | ✅ Fertig |
| `HmiManager` | Display (ST7789/SPI), Touch (AXS5106L/I2C), LVGL | ✅ Fertig |
| `I2CManager` | I2C-Bus-Scan, generischer Register-Zugriff, MCP23017-Grundsetup | ✅ Fertig |
| `MqttManager` | MQTT-Verbindung, `availability`/LWT, Publish/Subscribe-Helfer | ✅ Fertig |
| `ValveController` | Kapselt V0–V5 als MCP23017-Ausgänge (on/off/state) | ✅ Fertig |
| `ValveTimer` | Laufzeit/Restlaufzeit je Ventil, `maxTime`-Obergrenze | ✅ Fertig |
| `ConfigStore` | Persistenz in drei getrennten SPIFFS-Dateien: `config.json` (`time`/`auto`/`alias`/`maxTime`), `programs.json` (Programme + `activeProgram`, Phase 14), `schedule.json` (Zeitplan, Phase 15), je eigene JSON-Serialisierung für `main/config`\|`programs`\|`schedule`/`state` | ✅ Fertig (config, programs) / 📋 Phase 15 |
| `Sequencer` | Automatik-Ablauf V1→V5 (`main/cmd`), Fortsetzung bei manuellem Aus/`maxTime` | ✅ Fertig |
| `HaDiscovery` | Home-Assistant-MQTT-Discovery-Configs | 📋 Phase 10 |
| `Diagnostics` | `i2cStatus`/`lastError` | ✅ Fertig |

`main.cpp` bleibt ein schlanker Orchestrator (`setup()`/`loop()` ruft die Manager-Klassen auf).

### Log-Format (`Logger`)

```
hh:mm:ss:mmm CLASS TYPE logtext
```

- `hh:mm:ss:mmm`: Laufzeit-/Uhrzeit-Format. Bis zur ersten erfolgreichen NTP-Synchronisierung beim Boot (siehe `WifiManager::connectAndSyncTimeBlocking()`, Phase 2) boot-relative Zeit seit Start (`millis()`-basiert); danach Echtzeituhr.
- `CLASS` (5 Zeichen, rechts mit Leerzeichen aufgefüllt): `WIFI `, `MQTT `, `I2C  `, `HMI  `, `SYS  `
- `TYPE` (5 Zeichen, rechts mit Leerzeichen aufgefüllt): `ERROR`, `INFO `, `DEBUG`, `PUB  `, `SUB  ` (`PUB`/`SUB` = ausgehende/eingehende MQTT-Nachrichten, siehe `MqttManager`)
- Jeder `ERROR`-Eintrag landet zusätzlich automatisch in `diagnostics/lastError` (siehe `Diagnostics`, Phase 8) — über `Logger::setErrorCallback()`, ohne dass `Logger` `Diagnostics` kennt.

Beispiel: `00:00:01:909 I2C   INFO  I2C-Scan gestartet...`

## Home-Assistant-MQTT-Discovery (Phase 10, geplant)

Alle Discovery-Configs teilen sich ein `device`-Objekt (`identifiers: ["gartenwasser"]`, Name „Gartenbewässerung“, Hersteller „Pf@nne“) und referenzieren `gartenwasser/availability` (online/offline), damit alle Entities auf einer Geräteseite in Home Assistant gebündelt sind.

| HA-Komponente | Object-ID | Topics |
|---|---|---|
| `binary_sensor` | `V0_state` | state: `V0/state` |
| `switch` | `V{n}_cmd` | cmd: `V{n}/cmd`, state: `V{n}/state` |
| `number` | `V{n}_time` | set: `V{n}/time/set`, state: `V{n}/time/state` |
| `sensor` | `V{n}_remaining` | state: `V{n}/time/remaining` |
| `switch` | `V{n}_auto` | set: `V{n}/auto/set`, state: `V{n}/auto/state` |
| `switch` | `main_cmd` | cmd: `main/cmd`, state: `main/state` |
| `sensor` | `main_activeValve` | state: `main/activeValve` |
| `sensor` | `main_remainingTotal` | state: `main/remainingTotal` |
| `sensor` | `diag_i2cStatus` | state: `diagnostics/i2cStatus` |
| `sensor` | `diag_lastError` | state: `diagnostics/lastError` |

Discovery-Configs werden retained unter `homeassistant/<component>/gartenwasser/<object_id>/config` nach jedem erfolgreichen MQTT-Connect neu publiziert.

## Entscheidungshistorie

- Konfiguration per MQTT (2026-08-15): statt eines separaten `main/reset`-Topics mit fest hinterlegten Presets (`DEFAULT`/`TEST`) gibt es `main/config/set`/`main/config/state` (volle Konfiguration als JSON, lesen/schreiben, Teil-Updates möglich). Presets sind damit einfach extern gespeicherte JSON-Payloads statt Firmware-Code. Ersetzt außerdem die für Phase 11 geplanten Sammel-Befehle `main/time/set`/`main/auto/set`. Noch nicht implementiert (siehe `docs/spec/11-sammelbefehle.md`).
- Bewässerungsprogramme im Backlog ergänzt (2026-08-15): benannte Presets (`time` **und** `auto` je Ventil, z. B. `SHORT`/`MEDIUM`/`LONG`/`TEST`) als Array in `config.json`, auswählbar per `main/program/cmd <integer>` (Phase 14, baut auf Phase 7 + Phase 11 auf). `auto` bewusst mit im Programm, um z. B. „nur Rasen, nicht die Beete” abzubilden.
- Phase 15 von „Wochenplan” auf generischen „Zeitplan/Scheduler” erweitert (2026-08-15): Tages- und Wochenplan sind keine getrennten Features, sondern beides Trigger-Typen (`daily`/`weekly`/`once`) desselben Zeitplan-Mechanismus — eine beliebig lange, über `main/config/set` editierbare Liste von Einträgen (Trigger-Regel + Programm-Referenz), nicht auf 7 feste Wochentags-Slots begrenzt. Deckt damit auch „jeden Tag 21:00 Uhr”, „jeden Dienstag” und „genau am 01.02.26, 11:00 Uhr” ab. Bewusst früh dokumentiert, damit die Umsetzung von Sequencer (Phase 7), Konfiguration (Phase 11) und Programmen (Phase 14) nicht in eine Richtung läuft, die einen späteren, generischen Scheduler erschwert. Details (Trigger-Schema, verpasste Trigger, Konflikte bei Gleichzeitigkeit) bewusst noch offen. Noch nicht implementiert (siehe `docs/spec/14-programme.md`, `docs/spec/15-wochenplan.md`).
- Priorisierung: Phase 10 (Home Assistant MQTT-Discovery) ans Ende gestellt (2026-08-15): alle bisherigen Phasen sind rein geräteintern (Firmware/MQTT direkt), Phase 10 ist die erste mit einer externen Integration (Home Assistant). Erst alles Geräteinterne fertigstellen (Phasen 11–15), dann die externe Anbindung. Phasennummer/Dateiname bleiben unverändert (`docs/spec/10-ha-discovery.md`), nur die Bearbeitungsreihenfolge in `docs/README.md` wurde angepasst.
- Design für Bewässerungsprogramme abgestimmt (2026-08-16): `programs`-Array + `activeProgram`, `time`/`auto` je Programm als Teilmengen mit identischer Semantik wie `main/config/set` (enthaltene Felder werden übernommen, fehlende bleiben unverändert — bewusst keine Sonderregel für `auto`). Anwenden eines Programms ruft dieselben `applyTimeValue()`/`applyAutoValue()`-Kernfunktionen wie `main/config/set` auf. `maxTime`/`alias` sind kein Teil eines Programms. Obergrenze 8 Programme (`ConfigStore::kMaxPrograms`). Details siehe `docs/spec/14-programme.md`. Anbindung der Touch-UI-Buttons `P1`–`P4` folgt als eigener Schritt danach. Noch nicht implementiert. **Teilweise überholt durch den folgenden Eintrag** (eigene Datei/Topics statt Teil von `config.json`/`main/config/set`).
- Konfiguration in drei Bereiche aufgeteilt — `config`/`programs`/`schedule` (2026-08-16): statt einer wachsenden gemeinsamen `config.json`/`main/config/set`-Struktur bekommt jeder Bereich seine eigene SPIFFS-Datei und sein eigenes `.../set`/`.../state`-Topic-Paar (`main/config/*`, `main/programs/*`, später `main/schedule/*`), da sie unterschiedlich oft und aus unterschiedlichen Gründen geändert werden und sonst Puffer-/JSON-Capacity immer weiter für alle drei zusammen wachsen müssten. `config.json` bleibt bei `time`/`auto`/`alias`/`maxTime` (Umfang unverändert zu Phase 11). `programs.json` bekommt zusätzlich zum Programme-Array auch `activeProgram` (gehört inhaltlich zu den Programmen, nicht zu den Ventilparametern) — die schlanke Einzelwert-Auswahl `main/program/cmd`/`state` (Singular) bleibt daneben bestehen. `schedule.json`/`main/schedule/set`/`state` für Phase 15 von vornherein als eigener Bereich reserviert. Betrifft nur Design/Dokumentation, noch keine Codeänderung. Details siehe `docs/spec/11-sammelbefehle.md`, `docs/spec/14-programme.md`, `docs/spec/15-wochenplan.md`.
- Phase 14 umgesetzt, Stack-Overflow-Bug gefunden und gefixt (2026-08-16): `ConfigStore`/`MqttManager` gemäß obigem Design implementiert, automatisiert per Python/paho-mqtt getestet (14/14 Checks). Dabei einen echten Bug gefunden: `main/programs/set` liess das Board abstürzen (`Guru Meditation Error: Stack protection fault` in der `loopTask`), weil mehrere 2048-Byte-JSON-Puffer gleichzeitig auf dem mit 8192 Byte knapp bemessenen Standard-Stack lagen. Fix: `SET_LOOP_TASK_STACK_SIZE(16*1024)` in `main.cpp`. Details siehe `docs/spec/14-programme.md`, Abschnitt „Test/Ergebnis".
- `shortcut`-Feld für Programme ergänzt, zur Vorbereitung der `P1`–`P4`-Touch-UI-Anbindung (2026-08-16): optionales String-Feld je Programm (`"P1"`–`"P4"`), bindet ein Programm an einen physischen Button unabhängig von seiner Array-Position — sonst würde ein Umsortieren via `main/programs/set` (Array-Replace) stillschweigend die Button-Belegung verschieben. Doppelt vergebene Shortcuts werden bewusst nicht beim Schreiben abgelehnt (würde dem Array-Replace-Prinzip widersprechen), sondern beim Auflösen löst der erste Treffer in Array-Reihenfolge, zusätzlich mit einem nicht-blockierenden Log-Hinweis bei Duplikaten. Intern als `uint8_t` (0/1–4) gespeichert. Details siehe `docs/spec/14-programme.md`, Kernentscheidung 8.
- `P1`–`P4`-Anbindung umgesetzt, plus Grundsatzentscheidung „Automatik erfordert Programm“ (2026-08-16): Touch-Buttons wenden per `shortcut` gebundene Programme an, erneuter Druck auf ein aktives Programm wählt ab (Toggle, ruft intern dieselbe Logik wie `main/program/cmd 0`). Beim interaktiven Hardware-Test fiel auf, dass `main/cmd ON` bis dahin unqualifiziert die aktuellen `auto`-Flags abfuhr, unabhängig von jeder Programmwahl — ein Rest aus Phase 7, das es vor den Programmen noch nicht anders geben konnte. Entscheidung: **`main/cmd ON` startet nur noch, wenn ein Programm gewählt ist** (`activeProgram != 0`), sowohl per Touch als auch per MQTT (spätere Home-Assistant-Automatisierungen eingeschlossen) — eine „Automatik“ ohne definierten Rahmen hat kein sinnvolles Ziel mehr. Direktes Ventilschalten (`V{n}/cmd`) ist bewusst **nicht** betroffen, bleibt der uneingeschränkte manuelle Weg. Statuszeile zeigt bei keinem gewählten Programm „MANUELL“ statt des zuvor irreführenden „Bereit“ (das suggerierte fälschlich, `AUTO` sei einsatzbereit). Details siehe `docs/spec/07-automatik-sequenz.md` und `docs/spec/13-touch-ui.md`, jeweils Nachtrag.

Ursprünglich als „Offene Punkte” zur Diskussion gestellt, mittlerweile entschieden und oben in die jeweiligen Abschnitte eingearbeitet (Datum: 2026-08-14):

- Verbindungsabbruch während Automatik/Ventilbetrieb → läuft lokal autonom weiter.
- Manuelles Ein-/Ausschalten während Automatik → Ein ignoriert, Aus wird angenommen (Sequenz macht mit nächstem Ventil weiter).
- Mehrere Ventile gleichzeitig offen → erlaubt.
- I2C-/Stromausfall → durch Hardware bereits sicher (Ventile öffnen nur unter Spannung).
- `maxTime`-Obergrenze → pro Ventil (`min(time, maxTime)`), keine separate Gesamtsequenz-Überwachung nötig, da implizit auf `5 × maxTime` begrenzt.
- Boot-Grundzustand → alle Ventile aus, keine automatische Fortsetzung der Automatik.
- Persistenz → alle Einstellwerte (`time`, `auto`, `alias`, `maxTime`) dauerhaft im SPIFFS.
- Touch-UI → AUTO/OFF-Toggle + Ventil-Statusanzeige, eigene Phase 13.
- Echtzeituhr → NTP-Sync ergänzt (Phase 2).
- Validierung von MQTT-Befehlswerten → ungültige/außerhalb-Bereich-Werte werden ignoriert und geloggt.
- Netzwerksegment (`192.168.10.x` vs. `192.168.1.123`) → kein Problem, nur temporär: Broker im Heimnetz, Device aktuell im Ferienhaus, beide Router per VPN verbunden.
