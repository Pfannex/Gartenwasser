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

### Touch-UI (Phase 13, geplant)

- Toggle-Button „AUTO“/„OFF“ auf dem Display, gekoppelt an `main/cmd`.
- Statusanzeige der Ventile (`V0`–`V5`) auf dem Display.

## MQTT-Topic-Struktur

```
gartenwasser/
├── availability                    online|offline    (LWT)
├── V0/
│   └── state                       ON|OFF             (read-only)
├── V1/ .. V5/
│   ├── state                       ON|OFF             (read-only, Ist-Zustand)
│   ├── cmd                         ON|OFF             (Befehl, von HA)
│   ├── alias                       "Rasen Seite"      (Klartextname)
│   │   └── set                     "Text"             (Editieren des Alias-Namens)
│   ├── time/
│   │   ├── state                   <Minuten>          (aktuell eingestellte Laufzeit)
│   │   ├── set                     <Minuten>          (Befehl, von HA)
│   │   └── remaining               mm:ss              (Restlaufzeit, Sekundentakt)
│   └── auto/
│       ├── state                   ON|OFF             (Automatik-Flag Ist)
│       └── set                     ON|OFF             (Automatik-Flag Befehl)
├── main/
│   ├── cmd                         ON|OFF             (Start/Stop Befehl)
│   ├── state                       ON|OFF             (Sequenz läuft?)
│   ├── activeValve                 "V1".."V5" | "-"   (aktives Ventil)
│   ├── remainingTotal              mm:ss              (Restzeit der gesamten Sequenz)
│   ├── time/
│   │   ├── set                     JSON               (Sammel-Befehl, setzt maxTime + time/set mehrerer Ventile)
│   │   └── maxTime                 <Minuten>          (Obergrenze pro Ventil, effektive Laufzeit = min(time, maxTime))
│   └── auto
│       └── set                     JSON               (Sammel-Befehl, setzt auto/set mehrerer Ventile)
└── diagnostics/
    ├── i2cStatus                   ok|error           (Status i2cBus / MCP23017)
    └── lastError                   <Text/Zeitstempel> (letzte Fehlermeldung)
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
| `ConfigStore` | Persistenz aller Einstellwerte (`time`, `auto`, `alias`, `maxTime`) im SPIFFS | ✅ Fertig (bisher nur `time`/`maxTime`) |
| `Sequencer` | Automatik-Ablauf V1→V5 (`main/cmd`), Fortsetzung bei manuellem Aus/`maxTime` | 📋 Phase 7 |
| `HaDiscovery` | Home-Assistant-MQTT-Discovery-Configs | 📋 Phase 10 |
| `Diagnostics` | `i2cStatus`/`lastError` | 📋 Phase 8 |

`main.cpp` bleibt ein schlanker Orchestrator (`setup()`/`loop()` ruft die Manager-Klassen auf).

### Log-Format (`Logger`)

```
hh:mm:ss:mmm TYPE CLASS logtext
```

- `hh:mm:ss:mmm`: Laufzeit-/Uhrzeit-Format. Bis Phase 2 (NTP-Sync) boot-relative Zeit seit Start (`millis()`-basiert); danach Echtzeituhr.
- `TYPE` (5 Zeichen, rechts mit Leerzeichen aufgefüllt): `ERROR`, `INFO `, `DEBUG`
- `CLASS` (5 Zeichen, rechts mit Leerzeichen aufgefüllt): `WIFI `, `MQTT `, `I2C  `, `HMI  `

Beispiel: `00:00:01:909 INFO  I2C   I2C-Scan gestartet...`

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

Ursprünglich als „Offene Punkte“ zur Diskussion gestellt, mittlerweile entschieden und oben in die jeweiligen Abschnitte eingearbeitet (Datum: 2026-08-14):

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
