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
| `programs` | `/programs.json` | Bewässerungsprogramme | `main/programs/set` / `main/programs/state` (Bulk), `main/program/cmd` / `main/program/state` (Auswahl per Index) |
| `schedule` | `/schedule.json` | Zeitplan | `main/schedule/set` / `main/schedule/state` (Bulk), `main/schedule/cmd` (globaler Ein/Aus-Schalter), `main/schedule/cleanup` (abgelaufene `once`-Einträge entfernen) |

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
    {"name": "Kurz",  "time": {"V1": 2, "V2": 2}, "auto": {"V1": true, "V2": true, "V3": false, "V4": false, "V5": false}},
    {"name": "Rasen", "time": {"V1": 10, "V2": 10}, "auto": {"V1": true, "V2": true, "V3": false, "V4": false, "V5": false}},
    {"name": "Alles", "time": {"V1": 8, "V2": 8, "V3": 12, "V4": 15, "V5": 6}, "auto": {"V1": true, "V2": true, "V3": true, "V4": true, "V5": true}},
    {"name": "Test",  "time": {"V1": 1, "V2": 1, "V3": 1, "V4": 1, "V5": 1}, "auto": {"V1": true, "V2": true, "V3": true, "V4": true, "V5": true}}
  ],
  "activeProgram": 2
}
```

`time`/`auto` je Programm sind Teilmengen — was drinsteht, wird beim Anwenden übernommen, was fehlt, bleibt unverändert (dieselbe Semantik wie bei `main/config/set`). `activeProgram` ist 1-basiert (`0` = kein Programm gewählt), und lebt bewusst hier (nicht in `config`), weil er sich auf die Programme bezieht. `maxTime` und `alias` sind bewusst kein Teil eines Programms. Die schlanke Einzelwert-Auswahl `main/program/cmd <n>` (Singular) bleibt zusätzlich bestehen — bequemer Weg z. B. für die Touch-UI, ohne JSON senden zu müssen; intern dieselbe Wirkung wie `activeProgram` über `main/programs/set` zu setzen. Bis zu 32 Programme möglich (`ConfigStore::kMaxPrograms`, ursprünglich 8, siehe Entscheidungshistorie). Das anfangs vorgesehene `shortcut`-Feld (`"P1"`–`"P4"`, band ein Programm an einen physischen Touch-Button) wurde mit der Touch-UI-Neugestaltung (siehe Abschnitt „Touch-UI“ unten) wieder entfernt — die Programme-Unterseite blättert stattdessen durch alle Programme. Details siehe `docs/spec/14-programme.md`.

**`schedule`** (`main/schedule/state`, Phase 15, fertig): Array von Zeitplan-Einträgen (Trigger-Typ `daily`/`weekly`/`once`, Programm-Referenz per `program`-Feld **per Name** statt Array-Index, um Drift bei `main/programs/set`-Umsortierungen zu vermeiden — analog zum `shortcut`-Feld der Programme). `program` darf sich über mehrere Einträge wiederholen (z. B. dasselbe Programm täglich **und** zusätzlich an einem Wochentag) — bewusst keine Eindeutigkeitspflicht, auch nicht über ein zusätzliches ID-Feld, da anders als bei Programmen nichts von außen einzeln auf einen Zeitplan-Eintrag zeigt (immer Array-Replace als Block). `program` ist zugleich der einzige Identifikator eines Eintrags — ein separates `name`-Feld gab es kurzzeitig, wurde aber als redundant wieder entfernt (2026-08-16). Globaler Ein/Aus-Schalter (`enabled` auf oberster Ebene + Convenience-Topic `main/schedule/cmd`, analog `main/program/cmd`). Verpasste Trigger (Reboot im Startfenster) verfallen bewusst, statt nachgeholt zu werden. Vollständige Feldreferenz + weitere Beispiele siehe `docs/spec/15-wochenplan.md`.

### Touch-UI (Phase 13/14, neu gestaltet 2026-08-17, fertig)

- Titelzeile „Gartenwasser“ (grau hinterlegt, rein statisch), darunter START/STOP-Toggle-Button (volle Breite), gekoppelt an `main/cmd` (startet nur mit gewähltem Programm, siehe „Automatisierungsfunktionen“ oben).
- Ventile `V0`–`V5` als 4×4-Statusmatrix (grün/rot/dunkelgrau bei `auto=OFF` + AUS) statt der früheren LED-Liste — 10 weitere Zellen bleiben als Platzhalter für eine mögliche spätere 16-Ventil-Erweiterung sichtbar. `V1`–`V5` sind per Tap direkt schaltbar (`V{n}/cmd`, ohne MQTT-Umweg), `V0` nicht (wie bei MQTT ohne eigenen `cmd`).
- Programme-Button unter der Matrix zeigt das aktive Programm als Buttontext und öffnet eine eigene Unterseite: `<`/`>` blättert durch alle Programme (inkl. „Kein Programm“), OK wendet nur an (startet nichts — Start bleibt ein separater Schritt), Abbrechen verwirft.
- Zweizeilige Statuszeile (Fußleiste) zeigt priorisiert, was gerade passiert: Fehler (roter Hintergrund/gelbe Schrift) > transienter Hinweis (2s, z. B. „Kein Programm vorgewählt!“, orange) > laufende Automatik (Ventil-Restlaufzeit | Sequenz-Restlaufzeit gesamt, gelb) > manuell geschaltetes Ventil („MANUELL“, hellblau) > sonst leer; Zeile 2 zeigt jeweils den Alias-Namen des betroffenen Ventils.
- Die früheren 4 festen „P1“–„P4“-Buttons (samt `shortcut`-Feld der Programme) wurden dabei komplett entfernt — ersetzt durch die Programme-Unterseite, die nicht mehr auf 4 gebundene Programme begrenzt ist. Details siehe `docs/spec/13-touch-ui.md`.

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
│   └── schedule/      |        |                    |
│       ├── set        | nein   | JSON               | Zeitplan-Array + enabled setzen (Array wird komplett ersetzt)
│       ├── state      | ja     | JSON               | Aktueller Gesamtstand von schedule (retained)
│       ├── cmd        | nein   | ON|OFF             | Globaler Ein/Aus-Schalter (Convenience, analog main/program/cmd)
│       └── cleanup    | nein   | beliebig           | Entfernt abgelaufene "once"-Einträge (Einmalbefehl)
├── main/info/         |        |                    | Hardware-/Systeminfo (Info-Seite), getrennt von diagnostics/ oben
│   ├── resetReason    | ja     | z.B. "USB"         | Grund des letzten Neustarts (esp_reset_reason()), einmalig pro Boot
│   ├── uptime         | ja     | <Sekunden>         | Laufzeit seit Boot, alle 30s aktualisiert
│   ├── stackFree      | ja     | <Byte>             | Freier loopTask-Stack (uxTaskGetStackHighWaterMark), alle 30s
│   ├── rssi           | ja     | <dBm, negativ>     | WLAN-Signalstärke, alle 30s
│   ├── ip             | ja     | z.B. "192.168.10.33" | Eigene IP-Adresse, einmalig pro Boot
│   ├── broker         | ja     | z.B. "192.168.1.123:1883" | MQTT-Broker-Adresse, einmalig pro Boot
│   └── partitions     | ja     | JSON-Array         | Partitionstabelle (dynamisch per esp_partition_find() ausgelesen), inkl. Belegung wo bestimmbar (app-Slots, webfs, config), einmalig pro Boot
└── diagnostics/       |        |                    |
    ├── i2cStatus      | ja     | ok|error           | Status i2cBus / MCP23017
    ├── lastError      | ja     | <Text/Zeitstempel> | letzte Fehlermeldung
    ├── version        | ja     | z.B. "V0.8.0.0"    | Firmware-Version (include/Version.h), zum Bestaetigen eines OTA-Updates (Phase 21)
    ├── ram            | ja     | z.B. "63% (206/328 KB)" | Heap-Nutzung, alle 30s aktualisiert (ESP.getFreeHeap()/getHeapSize())
    ├── flash          | ja     | z.B. "45% (1382/3072 KB)" | Sketch-Groesse vs. freier App-Slot (ESP.getSketchSize()/getFreeSketchSpace())
    └── livelog        | nein   | Log-Zeile (Text)   | jede Logger-Zeile (inkl. PUB/SUB), siehe Log-Format unten
        └── replay     | nein   | beliebig           | Einmalbefehl: kompletten aktuellen Log-Ringpuffer erneut senden
```

## Zugangsdaten

WLAN- und MQTT-Zugangsdaten liegen ausschließlich in `include/secrets.h` (nicht versioniert, siehe `.gitignore`). Vorlage: `include/secrets.h.example`.

| Parameter | Wert |
|---|---|
| MQTT-Broker | `192.168.1.123:1883` |
| MQTT Client-ID | `GardenWater` |

## Architektur (Firmware)

Die Firmware ist in eigenständige Klassen mit jeweils eigener `.h`/`.cpp`-Datei gegliedert (`src/`):

| Klasse/Namespace | Zuständigkeit | Status |
|---|---|---|
| `Logger` | Einheitliches Log-Format für alle Subsysteme | ✅ Fertig |
| `WiFiController` (Namespace) | WLAN-Verbindung, Reconnect, NTP-Sync beim Boot | ✅ Fertig |
| `HMI` | Display (ST7789/SPI), Touch (AXS5106L/I2C), LVGL | ✅ Fertig |
| `I2C` | I2C-Bus-Scan, generischer Register-Zugriff, MCP23017-Grundsetup | ✅ Fertig |
| `MQTT` (Namespace) | MQTT-Verbindung, `availability`/LWT, Publish/Subscribe-Helfer | ✅ Fertig |
| `ValveController` | Kapselt V0–V5 als MCP23017-Ausgänge (on/off/state) | ✅ Fertig |
| `ValveTimer` | Laufzeit/Restlaufzeit je Ventil, `maxTime`-Obergrenze | ✅ Fertig |
| `FileSystem` | Persistenz in drei getrennten LittleFS-Dateien: `config.json` (`time`/`auto`/`alias`/`maxTime`), `programs.json` (Programme + `activeProgram`), `schedule.json` (Zeitplan), je eigene JSON-Serialisierung für `main/config`\|`programs`\|`schedule`/`state` | ✅ Fertig |
| `AutomaticController` | Automatik-Ablauf V1→V5 (`main/cmd`), Fortsetzung bei manuellem Aus/`maxTime` | ✅ Fertig |
| `WebIF` | Web-Interface: liefert statische Dateien (LittleFS) per `ESPAsyncWebServer` aus, reines File-Serving (Architektur B) | ✅ Fertig (Dashboard, Phase 17) |
| `HaDiscovery` | Home-Assistant-MQTT-Discovery-Configs | 📋 Phase 10 |
| `Diagnostics` | `i2cStatus`/`lastError` | ✅ Fertig |

`main.cpp` bleibt ein schlanker Orchestrator (`setup()`/`loop()` ruft die einzelnen Klassen/Namespaces auf).

**Nachtrag (2026-08-18): Dateien/Klassen umbenannt** (Datei-für-Datei-Review durch den Nutzer, `Manager`-Suffix durchgaengig entfernt): `ConfigStore`→`FileSystem`, `HmiManager`→`HMI`, `I2CManager`→`I2C`, `MqttManager`→`MQTT`, `Sequencer`→`AutomaticController`, `WebManager`→`WebIF`, `WifiManager`→`WiFiController` (nicht `WiFi` — Namenskollision mit dem globalen `WiFi`-Objekt des ESP32-Arduino-Cores, das `WifiManager.cpp` selbst nutzt). `Diagnostics`, `Logger`, `ValveController`, `ValveTimer`, `main.cpp` unveraendert. Rein mechanische Umbenennung (Dateien, Klassennamen, `#include`s, alle Aufrufstellen) ohne Logikaenderung, komplett auf Hardware regressionsgetestet (`test_livelog_pubsub.py`, weiterhin PASS). **Wichtig für Nachschlagewerke**: bereits bestehende, datierte Log-/Nachtrag-Eintraege in `docs/Log.md` und den `docs/spec/*.md`-Dateien verwenden weiterhin die zum jeweiligen Zeitpunkt gueltigen alten Namen (Änderungsprotokoll, nicht rückwirkend umgeschrieben) — nur diese Architektur-Tabelle wurde aktualisiert.

### Log-Format (`Logger`)

```
hh:mm:ss:mmm CLASS TYPE logtext
```

- `hh:mm:ss:mmm`: Laufzeit-/Uhrzeit-Format. Bis zur ersten erfolgreichen NTP-Synchronisierung beim Boot (siehe `WifiManager::connectAndSyncTimeBlocking()`, Phase 2) boot-relative Zeit seit Start (`millis()`-basiert); danach Echtzeituhr.
- `CLASS` (5 Zeichen, rechts mit Leerzeichen aufgefüllt): `WIFI `, `MQTT `, `I2C  `, `HMI  `, `WEB  `, `SYS  `, `VALVE`, `SEQ  ` (`VALVE`/`SEQ` seit 2026-08-18, siehe Nachtrag unten)
- `TYPE` (5 Zeichen, rechts mit Leerzeichen aufgefüllt): `ERROR`, `INFO `, `DEBUG`, `PUB  `, `SUB  ` (`PUB`/`SUB` = ausgehende/eingehende MQTT-Nachrichten, siehe `MqttManager`)
- Jeder `ERROR`-Eintrag landet zusätzlich automatisch in `diagnostics/lastError` (siehe `Diagnostics`, Phase 8) — über `Logger::setErrorCallback()`, ohne dass `Logger` `Diagnostics` kennt.

Beispiel: `00:00:01:909 I2C   INFO  I2C-Scan gestartet...`

**Nachtrag (2026-08-18): Logging-Überarbeitung, Vorstufe zum geplanten Live-Log im Web-Interface.** Ein vollständiger Audit aller `Logger`-Aufrufstellen ergab: `Type::DEBUG` und `Source::HMI` waren definiert, aber nirgends benutzt; `Sequencer`/`ValveTimer` loggten gar nichts; Touch-UI-Aktionen liefen komplett über dieselben Funktionen wie MQTT und wurden deshalb immer als `Source::MQTT` geloggt statt als `HMI`; `Source::I2C` vermischte Bus-Gesundheit (Scan, MCP23017-Erreichbarkeit) mit tatsächlichen Ventilschaltungen. Entschieden (Nutzer-Vorgabe: „jedes manuelle Ereignis", „wichtigste interne Programmaktivitäten", „alle Pub/Sub-Events" loggen):
- Neue `Source`-Werte `VALVE` (Ventilschaltungen, aus `ValveController`/`ValveTimer` — vorher fälschlich `I2C`) und `SEQ` (Automatik-Sequenz-Lebenszyklus aus `Sequencer`: Start/Fortschritt/Ende, vorher komplett unsichtbar).
- `HmiManager` loggt jetzt bei jeder manuellen Touch-Aktion (START/STOP, Ventil-Matrix-Tap, Programm-OK) selbst eine `INFO/HMI`-Zeile, zusätzlich zur bestehenden Zeile der aufgerufenen Funktion — schließt die Lücke, dass `Source::HMI` nie benutzt wurde.
- `ConfigStore` loggt jetzt auch den **Erfolgsfall** beim Speichern aller drei Dateien (`config.json`/`programs.json`/`schedule.json`), vorher nur Fehler.
- **Bewusst nicht lösbar** (technische Grenze, keine Lücke): Web-Dashboard-Aktionen lassen sich nicht von generischen externen MQTT-Clients unterscheiden — der Browser spricht bei Architektur B direkt und identisch zu jedem anderen MQTT-Client mit dem Broker (siehe `docs/spec/16-webif-fundament.md`). Beide erzeugen bereits heute vollständige `SUB`-Log-Einträge für jede eingehende Nachricht — eine Unterscheidung bräuchte eine Payload-Markierung, die die MQTT-Kompatibilität mit Home Assistant/Skripten bräche, daher bewusst nicht umgesetzt.
- Alle Pub/Sub-Events waren bereits vollständig abgedeckt (`publishAndLog()` als einzige Publish-Stelle, `handleMqttMessage()` loggt jede eingehende Nachricht) — kein Handlungsbedarf.
- Auf Hardware verifiziert: alle betroffenen Aktionspfade (Ventil manuell, Konfiguration speichern, Programm anwenden, Automatik starten/stoppen) funktionieren nach der Umstellung weiterhin normal, keine Regressionen.

**Nachtrag (2026-08-18): Live-Log im Web-Interface umgesetzt.** Direkt im Anschluss an die Logging-Überarbeitung oben: jede Log-Zeile erreicht jetzt per neuem `Logger::setLineCallback()`-Hook (identisches Muster zu `setErrorCallback()`) einen Callback in `MqttManager`, der die fertig formatierte Zeile roh per `mqttClient.publish()` auf `gartenwasser/diagnostics/livelog` weiterreicht (bewusst **nicht** über `publishAndLog()` — das würde selbst wieder eine `PUB`-Zeile erzeugen und eine Rückkopplung auslösen). Ursprünglich waren `PUB`/`SUB` hier noch ausgeschlossen (angenommene Rückkopplungsgefahr) — **seit dem Nachtrag weiter unten korrigiert und aktiviert**, siehe dort für den Grund und einen dabei gefundenen Bug.

- **Ringpuffer (80 Zeilen, immer aktiv)**: jede Zeile landet zusätzlich in einem Ringpuffer in `MqttManager`. Grund: `diagnostics/livelog` ist bewusst nicht retained (reiner Stream, MQTT-Retain hielte ohnehin nur die letzte Zeile) — ohne Puffer sähe ein Web-Client nur Zeilen ab dem Moment des eigenen Verbindens, nichts von vorher (z. B. die komplette Boot-Sequenz).
- **Automatischer Replay** nach jedem erfolgreichen Broker-Connect (`connectToBroker()`) holt eine Verbindungslücke nach (Boot, oder ein späterer WLAN-/MQTT-Ausfall).
- **Anfrage-Replay** auf `gartenwasser/diagnostics/livelog/replay` (beliebiger Payload) — für Web-Clients, die die Log-Seite öffnen, während das Gerät schon länger verbunden ist und sonst (kein Retain) leer bliebe.
- **Neue eigenständige Web-Seite `data/log.html`/`data/log.js`** (eigener Navigation-Tab „Log“) statt einer Dashboard-Karte — Tabellenansicht (Zeit/Quelle/Typ/Event), Quelle/Typ als Spaltenkopf-Dropdown mit Mehrfachauswahl (Facetten-Prinzip: leere Auswahl = keine Einschränkung), Event-Spalte wird per Klick zum Live-Suchfeld (`Eventfilter: *Begriff*`-Label nach Verlassen, mit Lösch-Button, Enter verlässt das Feld wie ein Klick daneben).
- Drei CSS-Bugs dabei gefunden und behoben: (1) `overflow: hidden` auf der äußeren Karte schnitt die Filter-Dropdowns ab, unabhängig davon, an welchem Nachfahren sie hingen — behoben durch gezieltes Eckenabrunden der jeweils äußersten Kindelemente statt eines pauschalen `overflow: hidden`. (2) Kopf- und Körpertabelle wurden getrennt (statt `position: sticky` im `thead` innerhalb des scrollenden Bereichs), da ein schrumpfender Scrollbereich (weniger Zeilen nach Filterung) das Dropdown sonst mit abschnitt. (3) Das geerbte `text-transform: uppercase` der Spaltenkopf-Beschriftungen verfälschte versehentlich auch den eingetippten Freitext-Suchbegriff in der Anzeige (`0x` → `0X`) — der gespeicherte Wert war die ganze Zeit korrekt, nur die Anzeige betroffen, per gezieltem `text-transform: none` auf das Such-Label behoben.
- Auf Hardware verifiziert: keine `PUB`/`SUB`-Einträge im Live-Log, keine Flut/Rückkopplung, Boot-Puffer und Anfrage-Replay liefern korrekt nach, Filter/Suche funktionieren wie vorgesehen. Details siehe `docs/spec/17-webif-dashboard.md`, Nachtrag, `docs/testing.md`.
- **Korrektur (2026-08-18)**: Topic ursprünglich versehentlich unter dem Singular `diagnostic/livelog` angelegt (Nutzervorgabe, aber inkonsistent zu den bestehenden `diagnostics/i2cStatus`/`diagnostics/lastError`) — auf `diagnostics/livelog` (+ `.../replay`) korrigiert, direkt in die bestehende `diagnostics/`-Gruppe der Topic-Struktur oben einsortiert statt eines eigenen Astes.

**Nachtrag (2026-08-18): datei-für-datei-Review des Backend-Loggings, PUB/SUB-Durchleitung, PubSubClient-Reentrancy-Bug behoben.** Auf Nutzerwunsch systematisch jede `.cpp`-Datei einzeln durchgesehen ("was läuft im Backend, nicht die Frontend-Bedienung"), Funde nummeriert zur einfachen Kommentierung. Ergebnisse:
- `WifiManager.cpp`: bestehende `Source::SYSTEM`-Tags für Boot-Meldungen bewusst belassen (nicht auf `WIFI` verschärft).
- `WebManager.cpp`: `onNotFound`-Handler loggt jetzt `DEBUG/WEB` mit der angefragten URL — nützlich zum Aufspüren kaputter/veralteter Links, bewusst `DEBUG` statt `INFO`, da harmlose Anfragen wie `/favicon.ico` routinemäßig mit auflaufen.
- `ValveController.cpp`: `setAuto()`/`setAlias()` loggen jetzt `INFO/VALVE` (vorher unbeobachtet, da beide nur über `ConfigStore` persistierten ohne eigene Log-Zeile).
- Dabei explizit offen gelassen (kein Konsens nötig, siehe unten): ob `ConfigStore::setValveTime()`/`setMaxTime()`/`setActiveProgram()`/`setScheduleGlobalEnabled()` noch dedizierte Log-Zeilen brauchen — durch die PUB/SUB-Durchleitung (nächster Punkt) erscheint der resultierende Zustand ohnehin automatisch im Live-Log, zusätzliche Zeilen wären großteils redundant.
- **Architektur-Kurswechsel**: `PUB`/`SUB` erreichen `diagnostics/livelog` jetzt ebenfalls (vorher an der Quelle in `Logger::log()` ausgeschlossen, siehe Nachtrag oben — die dort angenommene Rückkopplungsgefahr war ein Denkfehler, `onLoggerLine()` nutzt ohnehin rohes `mqttClient.publish()` statt der geloggten `publishAndLog()`, erzeugt also nie neue Log-Zeilen). Einzige verbleibende Filterung: die zwei sekündlich (nicht retained) publizierenden Topics `main/remainingTotal` und `V{n}/time/remaining` — reines Rauschen während einer laufenden Automatik-Sequenz, ohne Zusatzinfo gegenüber `main/state`/`main/activeValve`.
- **Bug gefunden und behoben**: die PUB/SUB-Durchleitung deckte einen `PubSubClient`-Reentrancy-Fehler auf. `PubSubClient` nutzt einen gemeinsamen internen Puffer für ein- und ausgehende Pakete; das an `handleMqttMessage()` übergebene `topic` zeigt vermutlich direkt in diesen Puffer. Ein `mqttClient.publish()` **während** der Verarbeitung einer eingehenden Nachricht (hier: das Live-Log-Publish der gerade erzeugten `SUB`-Zeile, synchron aus dem Empfangs-Callback heraus) überschrieb diesen Puffer und machte `topic` für die anschließenden `strcmp()`-Vergleiche zu Datenmüll — auf Hardware reproduziert als sporadische `MQTT ERROR Unbekanntes Topic: '<Datenmüll>'`-Zeile direkt nach jeder legitimen `SUB`-Zeile. Fix: alle Live-Log-Publishes (neue Zeilen wie Replay) werden jetzt nur noch gepuffert (`pendingLogCount`/`deferredReplayRequested`), das tatsächliche `mqttClient.publish()` passiert ausschließlich in `MqttManager::flushPendingLogLines()`, aufgerufen von `MqttManager::loop()` direkt nach `mqttClient.loop()` — garantiert außerhalb jedes Empfangs-Callback-Kontexts.
- Auf Hardware verifiziert (`test_livelog_pubsub.py`): 108 Live-Log-Zeilen über Auto-Toggle, Programmwahl, Automatik-Start/-Stopp, 404-Aufruf — keine `Unbekanntes Topic`-Fehlzeilen mehr, `PUB`/`SUB` korrekt vertreten, die zwei Rausch-Topics korrekt gefiltert, alle Kommandos weiterhin korrekt zugestellt.
- `data/log.js`: `LOG_TYPES` um `PUB`/`SUB` ergänzt (Filter-Dropdown zeigt jetzt alle fünf Typen).
- **Review abgeschlossen** (`MqttManager.cpp`, `HmiManager.cpp`, `main.cpp` als letzte drei Dateien): `applyProgram()` loggt jetzt beim erfolgreichen Anwenden eines Programms `INFO/MQTT` "Programm '<Name>' angewendet." — vorher nur aus einer Reihe von Einzel-PUBs erschließbar, keine zusammenfassende Zeile für dieses zentrale Geschäftsereignis. `publishConfigStateAndClearProgram()` loggt zusätzlich, wenn der MANUELL-Reset tatsächlich auslöst: `INFO/MQTT` "MANUELL: Programm '<Name>' durch direkte Aenderung abgewaehlt." — sonst im Log nicht unterscheidbar, ob ein Nutzer bewusst "Kein Programm" gewählt hat oder die MANUELL-Konsistenz-Logik das als Nebenwirkung einer Zeit-/Auto-Änderung ausgelöst hat. `HmiManager::begin()` prüft jetzt den bisher ignorierten Rückgabewert von `gfx->begin()` und loggt `ERROR/HMI` "Display-Init fehlgeschlagen." bei Fehlschlag (`bsp_touch_init()` liefert kein Ergebnis, dort nichts prüfbar). `main.cpp` bleibt unverändert (reiner Orchestrator, `"Setup abgeschlossen."` deckt den Boot-Erfolg bereits ab).
- Auf Hardware verifiziert (`test_program_logging.py`): beide neuen Zeilen erscheinen korrekt und an der richtigen Stelle im Live-Log, keine Regressionen, Ausgangszustand wiederhergestellt.

**Nachtrag (2026-08-18): JSON-Pretty-Print im Live-Log, Log-Zeilen-Puffer 192/224 → 512/560 Byte.** `data/log.js` erkennt PUB/SUB-Zeilen mit JSON-Payload (`topic = {...}`/`[...]`) und stellt sie eingerückt statt als Einzeiler dar (`JSON.parse()` + `JSON.stringify(..., null, 2)`, mit eigenem Scrollbereich in der Event-Spalte). Schlägt das Parsen fehl, bleibt die Zeile unverändert als Rohtext stehen — kein Sonderfall nötig. Dabei aufgefallen: `Logger::logf()`s bisheriger 192-Byte-Puffer schnitt `main/config/state` (~250–450 Byte inkl. Topic) schon vorher mitten im String ab, Pretty-Print hätte für dieses Topic also nie gegriffen. Auf Nutzerentscheidung hin behoben statt hingenommen: `Logger::logf()`-Puffer auf 512 Byte angehoben (deckt `main/config/state` auch im Alias-Worst-Case ab), `Logger::log()`s Zeilenpuffer auf 560 Byte (neue öffentliche Konstante `Logger::kMaxLineLength`, von `MqttManager`s Ringpuffer-Zeilengröße referenziert statt eines eigenen, bisher nur zufällig gleichen Literals). `main/programs/state`/`main/schedule/state` bleiben bei mehreren Einträgen weiterhin abgeschnitten (potenziell mehrere KB, dafür wäre ein Einzeilen-Logformat grundsätzlich der falsche Ansatz) — Pretty-Print fällt dafür sauber auf Rohtext zurück. RAM-Mehrbedarf: 80 Zeilen × 336 Byte Differenz ≈ 26,9 KB (Nutzung 40,1% → 48,3%). Auf Hardware verifiziert: `main/config/state` kommt jetzt vollständig an, kompletter PUB/SUB-Regressionslauf weiterhin PASS.

**Nachtrag (2026-08-18): abgeschnittenes JSON im gleichen Kasten dargestellt, zweite Pufferrunde 512/560 → 1024/1088 Byte.** Nutzer-Feedback in zwei Runden: erst wirkte die abgeschnittene `main/programs/state`-Zeile als reiner Fließtext „kaputt" (behoben: auch ungültiges JSON landet jetzt im `.log-json`-Kasten, nur ungerückt mit gestricheltem Rand + „(gekürzt)"-Hinweis statt Einrückung — `parseJsonMessage()` in `data/log.js` liefert seither immer ein Ergebnis, sobald der Wert mit `{`/`[` beginnt, mit `truncated`-Flag). Zweite Rückmeldung stellte klar, dass das nicht das eigentliche Ziel war — „programs/state ist nicht pretty print dargestellt!" —, das eigentliche Problem blieb die Pufferbegrenzung. Gemessen: `main/programs/state` braucht bei 5 Testprogrammen bereits 604 Byte (Topic+Payload). Nutzerentscheidung nach Rücksprache mit konkreten RAM-Zahlen: `Logger::logf()`-Puffer 512→1024 Byte, `Logger::kMaxLineLength` 560→1088 Byte (deckt ca. 8–9 Programme; 2048/2112 Byte hätte ~86% RAM-Nutzung bedeutet und wurde als zu riskant verworfen). RAM-Nutzung 48,3% → 61,2%. Auf Hardware verifiziert: `main/programs/state` (5 Programme) kommt vollständig an, byte-identisch zum retained Wert, parsebar; kompletter PUB/SUB-Regressionslauf weiterhin PASS.

### OTA-Update (Phase 21, fertig)

Zwei unabhängige HTTP-POST-Endpunkte in `WebIF.cpp` (`/api/ota/firmware`, `/api/ota/filesystem`), bewusste Ausnahme vom „reines File-Serving"-Prinzip aus Phase 16 — Firmware/Dateisystem sind mit 1–2 MB für MQTT (bei uns auf wenige KB gedeckelt) die falsche Größenordnung. Nutzen die ESP32-`Update`-Bibliothek: `U_FLASH` schreibt in den jeweils inaktiven `app0`/`app1`-Slot, `U_SPIFFS` findet automatisch die Partition mit SubType `spiffs` (`webfs`) — die `config`-Partition (persönliche Einstellungen) hat einen anderen SubType und bleibt dadurch automatisch unberührt, ganz ohne Extra-Schutzlogik. Zwei getrennte Uploads statt eines gebündelten Archivs (Nutzer-Entscheidung nach kurzer Diskussion, siehe `docs/spec/21-webif-ota.md`) — PlatformIO erzeugt beide Artefakte ohnehin getrennt. Neue `Logger::Source::OTA` für einheitliches Logging unabhängig vom Übertragungsweg. Nach erfolgreichem Update: verzögerter `ESP.restart()` über `WebIF::loop()` (500 ms Verzögerung, damit die HTTP-Erfolgsantwort den Browser noch erreicht). Details, inkl. eines dabei gefundenen Bugs (fehlendes `WebIF::loop()` in `main.cpp` verhinderte zunächst jeden Neustart), siehe `docs/spec/21-webif-ota.md`.

Zweiter Teil, `src/OTA.h`/`.cpp`: `ArduinoOTA`-Bibliothek für den eigenen Dev-Workflow ohne Kabel (`pio run -e esp32-c6-devkitc-1-ota --target upload`/`uploadfs`, neues Environment in `platformio.ini`). mDNS-Ankündigung als `gartenwasser.local` (`WiFi.setHostname()` in `WiFiController.cpp` ergänzt), kein Passwort, Neustart nach Erfolg übernimmt die Bibliothek selbst. Auf Hardware verifiziert (echter `pio run --target upload` übers WLAN, `Result: OK`).

`include/Version.h` (`kFirmwareVersion`) + retained Topic `diagnostics/version` + Anzeige im Web-Dashboard dienen als eindeutiger Nachweis, dass ein Update tatsächlich übernommen wurde — für beide OTA-Wege.

**Nachtrag (2026-08-18): automatischer Build-Zähler.** `build_number.txt` (versioniert, ein Integer) wird von `tools/increment_build_number.py` — als PlatformIO-`extra_scripts`-Pre-Build-Hook eingebunden — bei jedem Build um 1 hochgezählt und daraus `include/BuildNumber.h` (`kBuildNumber`, 5-stellig, z. B. `"00004"`) generiert; die Header-Datei selbst ist reines Build-Artefakt (`.gitignore`). `diagnostics/version` zeigt seither `"<kFirmwareVersion> Build <kBuildNumber>"` (z. B. `"V0.8.0.0 Build 00004"`), ebenso die Boot-Log-Zeile. `kFirmwareVersion` bleibt weiterhin manuell gepflegt, nur die Build-Nummer läuft automatisch mit. Ein `pio run` ohne `-e` baut beide Environments (siehe oben) und zählt dadurch zweimal hoch — kein Fehler, nur bei genauer Zählung zu beachten.

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
- Phase 15 (Zeitplan): erster Design-Entwurf abgestimmt (2026-08-16): Programm-Referenz je Zeitplan-Eintrag **per Name statt Array-Index** (vermeidet dasselbe Drift-Problem, das die `shortcut`-Felder bei Programmen lösen). Verpasste Trigger (Reboot im Startfenster) verfallen bewusst, statt nachgeholt zu werden — Begründung: ein nachgeholter Trigger zu unvorhersehbarer Zeit wäre überraschender als ein ausgefallener Termin. Globaler Ein/Aus-Schalter für den kompletten Zeitplan (`enabled`-Feld + Convenience-Topic `main/schedule/cmd`, analog `main/program/cmd`). Zwei neue, noch zu verfeinernde Punkte notiert: ein nicht-blockierender Kollisions-Hinweis, wenn eine manuell gestartete Sequenz (`main/remainingTotal`) voraussichtlich noch läuft, sobald der nächste Zeitplan-Trigger fällig wird; sowie eine Aufräum-Funktion (Arbeitstitel `main/schedule/cleanup`) zum gezielten Entfernen abgelaufener `once`-Einträge auf Anfrage. Details siehe `docs/spec/15-wochenplan.md`. Noch nicht implementiert.
- Phase 15 (Zeitplan): Feldreferenz verfeinert, `program` vs. `name` geklärt (2026-08-16): kurz diskutiert, ob `name` gleichzeitig die Programm-Referenz sein könnte (ein Feld spart) — verworfen, weil `name` dann pro Eindeutigkeit nicht mehr zweimal dasselbe Programm referenzieren könnte (z. B. „Rasen" täglich **und** zusätzlich dienstags). Ebenso verworfen: ein zusätzliches eindeutiges `index`-Feld je Eintrag — anders als bei Programmen (`P1`–`P4`-Buttons, `main/program/cmd` brauchen eine stabile, von außen ansprechbare Referenz) zeigt beim Zeitplan nichts von außen auf einen einzelnen Eintrag, da `main/schedule/set` immer die komplette Liste als Block ersetzt. Ergebnis: eigenes `program`-Feld (Name-Referenz, darf sich wiederholen, keine Eindeutigkeitspflicht) bleibt bestehen, `name` ist optional und rein kosmetisch. Vollständige Feldreferenz-Tabelle + zusätzliche Beispiele (Mehrfachnutzung eines Programms, pausierter Einzeleintrag, minimaler Eintrag ohne `name`, komplett pausierter Zeitplan, mehrere Wochentage in einem Eintrag) in `docs/spec/15-wochenplan.md` ergänzt.
- Phase 15 (Zeitplan) umgesetzt und getestet (2026-08-16): Scheduler-Mechanik als minütlicher Prüfloop über die komplette `schedule`-Liste implementiert (`checkSchedule()`, analog zum bestehenden Ventil-Tick, läuft unabhängig von WLAN/MQTT). Löste dabei nebenbei die „gleichzeitige Trigger"-Frage ohne neue Logik (`Sequencer::isRunning()`-Guard lässt automatisch den ersten in Array-Reihenfolge gewinnen). `main/schedule/settingsError` verworfen (kein Fehlerfall mehr durch die genannte Auflösung), stattdessen der bestehende `diagnostics/lastError`-Kanal genutzt. Cleanup-Funktion als `main/schedule/cleanup` benannt und umgesetzt. Echter Zeit-Test (Trigger 2 Minuten in der Zukunft) bestätigte den kompletten Ablauf: Minute erreicht → Programm per Name aufgelöst → automatisch angewendet → Sequenz gestartet. Kollisions-Hinweis bei manuellem `main/cmd ON` sowie die dafür nötige „nächster Trigger"-Berechnung bewusst zurückgestellt, nicht Teil dieser Umsetzung. Details siehe `docs/spec/15-wochenplan.md`, `docs/testing.md`.
- Phase 15 (Zeitplan): `name`-Feld wieder entfernt (2026-08-16): das anfangs vorgesehene, rein kosmetische `name`-Feld je Eintrag wurde nach der ersten Umsetzung auf Nutzerwunsch wieder gestrichen — „alles basiert auf dem Key `program`", ein zusätzliches Beschriftungsfeld war redundant. `program` bleibt der einzige Identifikator eines Zeitplan-Eintrags. Auf Hardware verifiziert. Details siehe `docs/spec/15-wochenplan.md`, Abschnitt „Nachtrag".
- Touch-UI komplett neu gestaltet, `P1`–`P4`/`shortcut` entfernt (2026-08-17): mehrstufiger Design-Dialog — Programm-Anzahl-Limit (4 Buttons) hinterfragt, `kMaxPrograms` 8→32 angehoben; eine mögliche Erweiterung auf 16 Ventile (volle MCP23017-Kapazität) sowie ein Web-Interface zur vollständigen Geräte-Konfiguration wurden als jeweils eigene, noch nicht begonnene Phasen abgegrenzt (technisches Risiko notiert: `MqttManager::parseValveTopic()` nimmt aktuell einstellige Ventilnummern an). Ergebnis: Toggle-Button „AUTO“/„OFF“ → „START“/„STOP“; die frühere vertikale LED-Liste wurde durch eine 4×4-Ventil-Statusmatrix ersetzt (`V0`–`V5` belegt, Rest als Platzhalter für die spätere 16-Ventil-Option); `V1`–`V5` sind darin per Tap direkt schaltbar (`V{n}/cmd`, neue `MqttManager::requestValveCmd()`); die festen `P1`–`P4`-Buttons entfielen komplett zugunsten einer neuen „Programme“-Unterseite (LVGL-Multi-Screen-Navigation, architektonisch neu für dieses Projekt) mit `<`/`>`-Blättern durch alle Programme — **OK wendet nur an, startet nicht** (Start bleibt bewusst ein separater Schritt über START auf der Hauptseite, war ein expliziter Korrekturpunkt im Design-Dialog). Damit wurde auch das `shortcut`-Feld der Programme (Phase 14) komplett entfernt, nicht nur ungenutzt gelassen — sein einziger Zweck (P1–P4-Bindung) entfiel. Die Statuszeile wurde zweizeilig (Ventil-Zeitinfo/„MANUELL“ + Alias-Name) und über viele Runden direkt am Gerät nachjustiert (Layout/Abstände sind aktuell funktional final, aber noch nicht final „hübsch“ — bewusst zurückgestellt, siehe `docs/spec/13-touch-ui.md`). Details siehe `docs/spec/13-touch-ui.md`, Nachtrag, und `docs/testing.md`.
- Phase 15 (Zeitplan) erneut auf Hardware verifiziert, Kollisions-Hinweis endgültig verworfen (2026-08-17): nach dem Touch-UI-Umbau nochmal ein echter Trigger-Test durchgeführt (`once`-Eintrag 5 Minuten in der Zukunft, Programm „V1“) — feuerte pünktlich, lief sauber durch. Der seit 2026-08-16 als Merker offene „Kollisions-Hinweis bei manuellem `main/cmd ON` nahe am nächsten Trigger" wurde dabei komplett gestrichen (nicht nur zurückgestellt): die dafür nötige „nächster fälliger Trigger"-Berechnung (Wiederkehr-Mathematik für `daily`/`weekly`) wäre unverhältnismäßig aufwendig für den gebotenen Nutzen. Der ohnehin schon vorhandene `Sequencer::isRunning()`-Guard in `startSequence()` deckt die eigentliche Sicherheitsanforderung bereits vollständig ab — er gilt nicht nur für zwei gleichzeitig fällige Zeitplan-Einträge (wie schon am 2026-08-16 festgestellt), sondern genauso für einen manuellen Start während eines geplanten Laufs und umgekehrt: „wer zuerst kommt, malt zuerst", der zweite Versuch wird abgewiesen und geloggt. Details siehe `docs/spec/15-wochenplan.md`, Abschnitt „Verworfen", und `docs/testing.md`.
- Touch-UI-Zeitplanbedienung endgültig verworfen, Web-Interface als Phasen 16–21 geplant und **vor** Phase 10 eingeordnet (2026-08-17): das 172×320px-Display ist für eine Zeitplan-Bedienung zu klein (Nutzer-Entscheidung) — Zeitplan-Bearbeitung bleibt vollständig dem Web-Interface vorbehalten. Ressourcen-Check vorab durchgeführt: `app0`-Partition (3 MB) aktuell 41,5 % belegt, Partitionstabelle hat bereits eine vollständige Dual-OTA-Auslegung (`app0`/`app1`, `otadata`) für ein späteres Firmware-Update über die Weboberfläche — keine Änderung nötig. Grobschätzung für das komplette Web-Interface (inkl. `ESPAsyncWebServer`+`AsyncTCP`, Handler-Code, `ElegantOTA` für Phase 21): ≈1,7 MB von 3 MB (≈56 %), reichlich Marge — Größen-Checkpoint direkt nach Phase 16 vorgesehen, da dort der größte Sprung erwartet wird, statt Ressourcen-Vorsorge als eigene Phase vorzuziehen. Visueller Stil „Dashboard Cards“ gewählt (farbige Statuschips, weiche Karten, nah an Home-Assistant-Formsprache) aus drei zur Auswahl gestellten Optionen. Sechs Phasen geplant, vom einfachsten zum komplexesten Datenmodell und OTA bewusst zuletzt (höchstes Risiko, unabhängig vom Rest): 16 Fundament & Architekturentscheidung (zentrale offene Frage: ESP32-hostet-eigene-API vs. Browser-spricht-direkt-per-MQTT-over-WebSocket-mit-dem-Broker — noch zu klären), 17 Status-Dashboard (read-only), 18 Konfiguration, 19 Programme, 20 Zeitplan, 21 Firmware-Update. Details siehe `docs/spec/16-webif-fundament.md` bis `docs/spec/21-webif-ota.md`, `docs/README.md` (Phasen-Übersicht).
- Phase 16 (Web-Interface-Fundament) umgesetzt, Architektur/Frontend/Dateisystem final entschieden (2026-08-17): Interview mit dem Nutzer ergab **Architektur B** (Browser spricht per MQTT-over-WebSocket direkt mit dem Broker, ESP32 liefert nur statische Dateien — Voraussetzung: WebSocket-Listener auf dem Mosquitto-Broker, separat einzurichten; Netzwerk-Hinweis für Zugriff von unterwegs siehe `docs/spec/16-webif-fundament.md`), **Alpine.js** als Frontend (kein Build-Schritt) und **`LittleFS`** statt `SPIFFS`. Neue Klasse `WebManager` (`ESPAsyncWebServer`, aktiv gepflegter `ESP32Async`-Fork) liefert reines File-Serving, keine REST-Endpoints. `ConfigStore` komplett auf `LittleFS.h` umgestellt. Flash-Checkpoint: 41,5 % → 43,1 % (deutlich unter der Schätzung).
- Phase 16: `uploadfs`/LittleFS-Bug gefunden und behoben, nicht nur umgangen (2026-08-17, direkt im Anschluss): `pio run --target uploadfs` (`mklittlefs`) erzeugt ein gültiges Image, aber `LittleFS.begin(true)` (Auto-Format bei Mount-Problemen) erkannte es beim ersten Boot fälschlich als ungültig und formatierte automatisch neu — dabei gingen sowohl die vorab hochgeladenen Web-Dateien als auch bereits gesetzte Testdaten verloren (unangekündigt, siehe vorheriger Eintrag). Per gezielter Diagnose (Mount ohne Auto-Format testweise probiert) isoliert: **`LittleFS.begin(false)` mountet dasselbe Image sauber, alle Dateien sofort sichtbar** — die Annahme, ein Auto-Format sei die sichere Voreinstellung, war die eigentliche Ursache. `ConfigStore::begin()` nutzt jetzt dauerhaft `LittleFS.begin(false)`; Kehrseite ist, dass eine wirklich leere/nie beschriebene Partition einmalig per `uploadfs` vorbereitet werden muss, sonst schlägt der Mount fehl (unkritisch für dieses eine, bereits mehrfach geflashte Gerät). Damit entfällt der zunächst nötige Workaround (Web-Dateien als Firmware-Strings eingebettet) wieder vollständig — `data/` ist die alleinige Quelle, per `uploadfs` ausgeliefert, skaliert damit auch für die größeren Dateien ab Phase 17 (Alpine.js, `mqtt.js`). Neue `Logger::Source::WEB`. Details siehe `docs/spec/16-webif-fundament.md`, `docs/testing.md`.
- Phase 17 (Status-Dashboard) umgesetzt und getestet (2026-08-17): Voraussetzung aus Phase 16 erfüllt — WebSocket-Listener auf dem Mosquitto-Broker (2.0.21, Debian-LXC unter Proxmox) vom Nutzer eingerichtet (`/etc/mosquitto/conf.d/websockets.conf`, Port 9001, `allow_anonymous true`, passend zum bestehenden anonymen 1883-Listener). `mqtt.min.js` (MQTT.js, 369 KB) und `alpine.min.js` (Alpine.js 3.x, 47 KB) per `curl` von `unpkg.com` bezogen und lokal in `data/` abgelegt (kein CDN zur Laufzeit) — beide zusammen ≈416 KB, komfortabel innerhalb der 1,875-MB-`LittleFS`-Partition. Neue `data/app.js`: Alpine-Komponente verbindet sich per `mqtt.connect()` direkt mit dem Broker (feste Adresse, liegt in einem anderen Netzsegment als die Geräte-IP), abonniert `gartenwasser/#`, pflegt reaktiven State für Ventile/Sequenz/Programm/Diagnostics. Farblogik der Ventilkacheln repliziert exakt `HmiManager::refreshValveStatus()` (rot=an, grün=auto+aus, dunkelgrau=nicht-auto+aus, `V0` nie gedimmt). Keine Firmware-Änderung nötig — `WebManager`s bestehendes `serveStatic()` reicht aus. Vor dem Browser-Test per `paho-mqtt` (`transport="websockets"`) die komplette Pipeline ohne Browser verifiziert (26 retained Nachrichten sofort empfangen). Vom Nutzer im Browser bestätigt: „ja, alles wie geplant!!“. Details siehe `docs/spec/17-webif-dashboard.md`, `docs/testing.md`.
- Hauptseiten-Redesign umgesetzt, Konfigurations-/Web-Dateien-Partition endgültig getrennt (2026-08-17): nach einem optischen Vorschlag (Artefakt, gleiche Farbwelt wie das bestehende Dashboard, aber großzügigeres Layout — Kopfzeile mit Navigation-Platzhaltern für Konfiguration/Programme/Zeitplan, großer runder START/STOP-Button, größeres Ventilraster, Programm-/Diagnostics-Karten) vom Nutzer bestätigt („cooles Design, damit starten wir!“) und in `data/index.html`/`style.css`/`app.js` umgesetzt — inkl. je einer eigenen Fortschrittsanzeige für Ventil- und Sequenz-Restlaufzeit sowie fett hervorgehobenem Programmnamen (Nutzer-Feedback nach dem ersten Blick). Dabei erneut auf das schon in Phase 16 gelöste Problem gestoßen, diesmal strukturell: `config.json`/`programs.json`/`schedule.json` lagen weiterhin auf derselben Partition wie die Web-Dateien, wodurch jeder `uploadfs`-Lauf (jedes Dashboard-Update) die Konfiguration erneut löschte (zweimal auf Hardware reproduziert, vom Nutzer bemerkt: „Programme und Namen sind wieder flöten gegangen“). **Endgültiger Fix**: `partitions.csv` in zwei Partitionen aufgeteilt — `webfs` (1,75 MB, Subtype `spiffs`, wird von `uploadfs` automatisch getroffen) und `config` (128 KB, bewusst anderer Subtype `0x40`, damit `uploadfs` sie nie automatisch trifft). `ConfigStore` mountet jetzt eine eigene `fs::LittleFSFS`-Instanz auf `config` (mit `begin(true)`, hier wieder sicher, da `uploadfs` diese Partition strukturell nie berührt), `WebManager` mountet die globale `LittleFS`-Instanz explizit auf `webfs`. Per echtem `uploadfs`-Testlauf nach erneutem Setzen der Testdaten verifiziert: Programme/Aliase bleiben jetzt dauerhaft erhalten. Details siehe `docs/spec/16-webif-fundament.md`, Abschnitt „Nachtrag".
- Phase 18 (Web-Interface: Konfiguration bearbeiten) umgesetzt und getestet (2026-08-17): erster Schreibpfad des Web-Interfaces (Phase 17 war rein lesend). Design vorab per Artefakt abgestimmt und über mehrere Runden verfeinert — dabei zwei Automatik-Ideen für `maxTime` bewusst wieder verworfen (erst „Summe aller Laufzeiten + 5 min“ als Vorschlag, dann „längste Einzel-Laufzeit + 5 min“ als abgeleiteter, schreibgeschützter Wert): der Nutzer entschied sich zurück auf ein bewusst manuell gesetztes Feld („der user muss maxtime einstellen! berücksichtige maxtime immer“), da `maxTime` als Failsafe (`min(time, maxTime)`, siehe oben) eine bewusste Sicherheitsentscheidung bleiben soll, kein errechneter Wert. Stattdessen macht die UI nur sichtbar, wenn eine Ventil-Laufzeit dadurch tatsächlich gedeckelt wird (`time > maxTime` → Laufzeit erscheint gelb mit „→ X min effektiv“-Hinweis) — die Deckelung selbst passiert unverändert firmwareseitig im `ValveTimer`. Bestätigungs-Feedback pro Feld ebenfalls iterativ verfeinert: ein textbasierter „✓ gespeichert“-Hinweis wurde als „verwirrend“ verworfen, danach ein optimistisches grünes Aufblitzen zugunsten eines klareren Zustands ersetzt — ein bearbeitetes Feld färbt sich beim Verlassen sofort rot (noch nicht per Geräte-Echo bestätigt) und blendet weich zur normalen Textfarbe zurück, sobald die Bestätigung eintrifft (bleibt ein Feld dauerhaft rot, wurde der Wert vom Gerät abgelehnt — ergibt sich automatisch aus dem Mechanismus, kein Sonderfall nötig). Neue `data/konfiguration.html`/`data/konfig.js` (eigene Alpine-Komponente, eigene MQTT-Verbindung, analog `app.js`), `data/style.css` um Formular-Basisstile und `--state-warning`-Token ergänzt. Keine Firmware-Änderung nötig — alle verwendeten Topics existierten bereits. Details siehe `docs/spec/18-webif-konfiguration.md`, `docs/testing.md`.
- Phase 19 (Web-Interface: Programme verwalten) umgesetzt und getestet (2026-08-18): erstes Listen-/Array-Datenmodell des Web-Interfaces (bisher nur Einzelwerte, Phase 18). Design vorab per Artefakt abgestimmt: Programmliste als Karten (Name, „Aktiv“-Badge, Ventil-Zusammenfassung inkl. gelber `maxTime`-Deckelungswarnung wie Phase 18), Editor mit Name + je Ventil Automatik-Switch/Laufzeit, „+ Neues Programm“ als gestrichelte Karte, zweistufige Inline-Löschbestätigung statt Browser-Dialog. Nach erstem Blick eine Korrektur: die obere Aktionsleiste (Aktivieren/Bearbeiten/Löschen) blendet sich jetzt während der Bearbeitung komplett aus („der Bearbeitungsmodus ist wie ein eigenes modales Fenster, nur OK/Abbrechen sind aktiv“). `main/programs/set` schickt beim Speichern/Löschen bewusst immer `programs` **und** `activeProgram` zusammen (nicht nur `programs`), da ein Löschen vor dem aktiven Programm dessen Array-Index verschiebt — bei getrenntem Senden (Teil-Update-Prinzip) bliebe der jetzt falsche `activeProgram`-Wert im Gerät stehen. Neue `data/programme.html`/`data/programme.js`. Keine Firmware-Änderung nötig (siehe aber sofortiger Folge-Eintrag unten). Details siehe `docs/spec/19-webif-programme.md`, `docs/testing.md`.
- „MANUELL“-Konsistenz über Phase 13/14/17/18 hinweg nachgezogen (2026-08-18): unmittelbar nach Phase 19 stellte der Nutzer fest, dass eine manuelle `auto`-Änderung auf der Konfigurationsseite (Phase 18) bei weiterhin angezeigtem Programmnamen inkonsistent wirkt — Analyse ergab, dass `startSequence()` das gewählte Programm ohnehin vor jedem Start erneut anwendet (Drift-Schutz, siehe Phase 14, Kernentscheidung 3), eine solche manuelle Änderung also beim nächsten Start ohnehin **stillschweigend verworfen** worden wäre, ohne dass die UI das je gezeigt hätte. Entscheidung (Nutzer): `auto` ist ausschließlich über Programme setzbar, jede direkte `time`/`auto`-Änderung (`V{n}/time/set`, `V{n}/auto/set`, `main/config/set`) setzt `activeProgram` jetzt sofort auf `0` zurück (neue `MqttManager::publishConfigStateAndClearProgram()`, wiederverwendet das bestehende `applyProgram(0)` inkl. automatischem Auto-Reset aller Ventile — „bei MANUELL müssten eigentlich alle Ventile grau sein“). `maxTime`/`alias` lösen bewusst keine Rücksetzung aus (kein Teil eines Programms). Umgesetzt: Automatik-Toggle aus der Web-Konfigurationsseite entfernt (Phase 18); Web-Dashboard-Headline zeigt „Manueller Modus“ statt „Automatik inaktiv“ ohne gewähltes Programm, Ventilkacheln zeigen die konfigurierte Laufzeit statt „nicht in Automatik“, START-Button in beiden Oberflächen (Web **und** Touch-UI) gesperrt, solange kein Programm gewählt ist — Touch-UI zeigt dafür jetzt „Manueller Modus“ als Programme-Button-Text (statt „Kein Programm“) und der bisherige transiente 2-Sekunden-Warnhinweis „Kein Programm vorgewählt!“ wurde komplett entfernt (gegenstandslos, da der gesperrte Button gar keinen Klick mehr auslöst). Details siehe `docs/spec/14-programme.md`, `docs/spec/13-touch-ui.md`, `docs/spec/17-webif-dashboard.md`, `docs/spec/18-webif-konfiguration.md` (jeweils Nachtrag 2026-08-18), `docs/testing.md`.
- Phase 20 (Web-Interface: Zeitplan verwalten) umgesetzt und getestet (2026-08-18): komplexestes Datenmodell des Web-Interfaces (typabhängige Felder `daily`/`weekly`/`once`, Programm-Referenz per Name statt Index — anders als bei Programmen (Phase 19) verschiebt ein Löschen/Bearbeiten hier also nichts anderes, `main/schedule/set` kann `schedule` unabhängig vom globalen `enabled` schicken). Design vorab per Artefakt abgestimmt: Karte je Eintrag mit Programmname als Headline, farbcodiertem Trigger-Badge, „Pausiert“-Zustand, sowie zwei bisher nirgends visuell behandelte Warnfälle sichtbar gemacht — abgelaufener `once`-Termin (gelb) und „Programm nicht gefunden“ bei umbenanntem/gelöschtem referenziertem Programm. Editor mit demselben „modalen“ Verhalten wie Phase 19 (Aktionsleiste blendet sich während der Bearbeitung aus). Nach erstem Blick ein Detail nachjustiert: eigener Button „Abgelaufene Timer löschen“ statt eines unauffälligen Textlinks. Neue `data/zeitplan.html`/`data/zeitplan.js`, `data/style.css` um Zeitplan-spezifische Stile ergänzt (Karten-/Listen-/Editor-Grundgerüst bewusst von Phase 19 wiederverwendet, nicht dupliziert). Navigation aller vier Seiten jetzt komplett aktiv (letzter `soon`-Platzhalter „Zeitplan“ entfernt), totes `nav.tabs a.soon`-CSS aufgeräumt. Keine Firmware-Änderung nötig. Details siehe `docs/spec/20-webif-zeitplan.md`, `docs/testing.md`.
- Dashboard-Hero neu strukturiert: "Nächster Termin" + Programm-Picker (2026-08-18): direkt im Anschluss an Phase 20 zwei Nutzerwünsche umgesetzt. Erstens eine "Nächster Termin"-Anzeige, bewusst rein browserseitig berechnet (`app.js`, neue Wiederkehr-Logik für `daily`/`weekly`/`once`) — ausdrücklich kein Firmware-Eingriff, anders als der am 2026-08-17 verworfene "Kollisions-Hinweis" (der eine echtzeit-blockierende Server-Prüfung gewesen wäre, hier ist es nur eine Anzeige). Zweitens eine grössere Hero-Neugestaltung: Klick auf den Programmnamen öffnet eine Auswahlliste aller Programme, die sofort anwendet (`main/program/cmd`, kein Bestätigungsschritt) und während laufender Automatik gesperrt ist; die bisherige "Automatik inaktiv"/"Manueller Modus"-Headline entfiel komplett (der Picker selbst zeigt den Zustand bereits — "Programm wählen" vs. Programmname); "Nächster Termin" zog in den Hero um, dort nur sichtbar wenn nichts läuft (macht sonst Platz für die Fortschrittsbalken); der Fußbereich wurde radikal verschlankt auf nur noch "Diagnostics" über volle Breite (die Karten "Aktives Programm" und "Nächster Termin" sind jetzt im Hero abgedeckt). Ein zusätzlich vorgeschlagenes Live-Log der seriellen Konsole wurde geprüft (`Logger` hat aktuell keinen MQTT-Publish-Pfad) und bewusst als eigener, deutlich aufwendigerer Backlog-Punkt zurückgestellt statt mit umgesetzt. Details siehe `docs/spec/17-webif-dashboard.md`, Nachtrag, `docs/Log.md`.

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
