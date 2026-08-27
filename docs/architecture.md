# Gartenwasser — Architektur & Zusammenhänge (nicht-C-Teil)

Ergänzt `docs/development.md` (Doxygen für die C++-Firmware) um den Teil, den Doxygen
strukturell nicht abbilden kann: YAML-Konfiguration (Home Assistant, openHASP-Plate) hat
keine Klassen/Funktionen/Aufrufbeziehungen — stattdessen dokumentiert dieses Dokument die
**Datenfluss- und Abhängigkeitsstruktur** zwischen Geräte-Firmware, MQTT, Web-Interface,
Home Assistant und dem openHASP-Touchpanel als Mermaid-Diagramme (rendern nativ in
GitHub/VS Code).

Wer welche konkrete Entity/welches konkrete Objekt betrifft: `docs/manual/usermanual.md`
Kapitel 8 (HA-Entities) und Kapitel 9 (Plate-Seiten) haben die vollständigen Tabellen —
hier geht es um die Struktur dahinter, nicht die einzelnen Namen.

## 1. System-Topologie

Wer mit wem spricht — und über welchen Weg. Der zentrale, oft übersehene Punkt: der Browser
(Web-Interface) verbindet sich für alle Live-Daten **direkt per MQTT-over-WebSocket mit dem
Broker**, nicht über einen Umweg über das Gerät (siehe `usermanual.md`, Kapitel 4.4/6) — das
Gerät liefert dem Browser nur die statischen HTML/JS-Dateien per HTTP.

```mermaid
graph LR
    subgraph GERAET["ESP32-C6 Geraet (Firmware)"]
        FW["Firmware-Kern<br/>ValveController / AutomaticController /<br/>ConfigStore / MqttManager / HaDiscovery"]
        FS["LittleFS: WebIF-Dateien<br/>(index.html, app.js, ...)"]
    end

    BROKER[("MQTT-Broker<br/>Mosquitto<br/>TCP 1883 + WebSocket 9001")]
    BROWSER["Browser<br/>Web-Interface (Kapitel 6)"]
    HA["Home Assistant<br/>Discovery + Dashboard + openhasp-Integration"]
    PLATE["openHASP-Touchpanel<br/>plate_wz (Kapitel 9)"]

    FW <-->|"MQTT TCP 1883<br/>alle gartenwasser/*-Topics"| BROKER
    BROWSER -->|"HTTP: statische Dateien"| FS
    BROWSER <-.->|"MQTT-over-WebSocket<br/>DIREKT, am Geraet vorbei"| BROKER
    HA <-->|"MQTT TCP 1883"| BROKER
    PLATE <-->|"MQTT TCP 1883<br/>hasp/plate_wz/#"| BROKER
    HA -->|"Property-Bindings + Events<br/>(openhasp.yaml)"| PLATE
```

**Konsequenz für die Fehlersuche:** Geht der Broker offline, sind Browser, HA und Plate
gleichermaßen betroffen — das Gerät läuft lokal autonom weiter (Ventile/Automatik/Zeitplan
laufen unabhängig von jeder Verbindung, siehe `docs/requirements.md`). Geht **nur** das
Gerät offline, laufen Broker/HA/Plate weiter, zeigen aber alle Geräte-Entities als
„nicht verfügbar" (LWT auf `gartenwasser/availability`, siehe `usermanual.md` Kapitel 8.1).

## 2. Home-Assistant-Konfigurationsschichten

Drei Schichten: **Datenquelle** (kommt vom Gerät), **Verarbeitung** (reine HA-Logik, kein
eigenes MQTT-Topic), **Präsentation** (was der Nutzer sieht/bedient). Pfeile zeigen
Abhängigkeit, nicht zeitliche Reihenfolge.

```mermaid
graph TB
    subgraph T1["Datenquelle (vom Geraet)"]
        DISC["MQTT-Discovery<br/>26 Entities"]
        MQTTY["mqtt.yaml<br/>25 ergaenzende Entities"]
    end

    subgraph T2["Verarbeitung (reine HA-Logik)"]
        TEMPLATE["template.yaml<br/>9 berechnete Sensoren"]
        HELPERS["input_number / input_select / input_boolean /<br/>input_text / input_datetime<br/>Formular-Entwuerfe + Plate-Bedienzustand"]
        SCRIPTS["scripts.yaml<br/>17 Skripte (CRUD-Logik)"]
        AUTOM["automations.yaml<br/>(gartenwasser + plate_wz), 4 Automationen"]
    end

    subgraph T3["Praesentation"]
        DASH["HA-Dashboard<br/>6 Views"]
        PLATEBIND["plate_wz/openhasp.yaml<br/>Objekt-Bindings + Events"]
    end

    DISC --> TEMPLATE
    DISC --> SCRIPTS
    DISC --> AUTOM
    MQTTY --> TEMPLATE
    MQTTY --> SCRIPTS

    TEMPLATE --> DASH
    TEMPLATE --> PLATEBIND
    HELPERS --> DASH
    HELPERS --> PLATEBIND
    HELPERS -.->|"Speichern/Abbrechen"| SCRIPTS
    SCRIPTS -->|"main/*/set Publish"| DISC
    AUTOM --> HELPERS
    AUTOM --> DASH

    PLATEBIND -->|"Tap-Events"| SCRIPTS
    PLATEBIND -->|"Tap-Events"| HELPERS
```

**Lesehilfe:** `SCRIPTS --> DISC` schließt den Kreis — ein Skript schreibt nicht direkt in
eine Anzeige-Entity, sondern publiziert auf ein `.../set`-Topic; das Gerät validiert,
übernimmt (oder verwirft, siehe `docs/Log.md` zu stillen Ablehnungen) und echot den neuen
Zustand zurück auf `.../state` — von dort fließt er ganz normal wieder durch `TEMPLATE`/
`DASH`/`PLATEBIND`. Kein Skript nimmt eine Abkürzung an der Firmware vorbei. Der Übersicht
halber nicht eingezeichnet: Dashboard und Plate lesen für einzelne Kacheln/Felder teils auch
direkt aus `DISC`/`MQTTY` (z. B. Firmware-Version, Alias-Textfelder) statt über `TEMPLATE`/
`HELPERS` — die Grafik zeigt die *Schichten-Rollen*, keine vollständige Kantenliste.

## 3. Datenfluss-Beispiel: Zeitplan-Eintrag am Plate bearbeiten

Konkretes Beispiel für den kompletten Rundlauf Plate → HA → MQTT → Firmware → MQTT → HA →
Plate, anhand des in dieser Session gefundenen und gefixten Mechanismus (atomare
`jsonl`-Property, siehe `docs/Log.md`, 2026-08-27).

```mermaid
sequenceDiagram
    participant User
    participant Plate as openHASP-Plate
    participant Broker as MQTT-Broker
    participant HA as Home Assistant
    participant FW as Geraete-Firmware

    Note right of Plate: Button p4b65 ("Einmalig")
    User->>Plate: Tippt "Einmalig"
    Plate->>Broker: hasp/plate_wz/state/p4b65<br/>{"event":"up"}
    Broker->>HA: (Event-Subscription)
    HA->>HA: script.gartenwasser_plate_zeitplan_typ_setzen<br/>baut neuen schedule-Eintrag
    HA->>Broker: gartenwasser/main/schedule/set<br/>{"schedule":[...]}
    Broker->>FW: (Subscription)
    FW->>FW: validiert Eintrag,<br/>uebernimmt in schedule.json
    FW->>Broker: gartenwasser/main/schedule/state<br/>{"schedule":[...]} (retained)
    Broker->>HA: sensor.gartenwasser_zeitplan aktualisiert sich
    HA->>HA: reaktive Vorlage berechnet neuen<br/>Text + Index fuer p4b61 (Eintrags-Dropdown)
    HA->>Broker: hasp/plate_wz/command/p4b61.jsonl<br/>{"options":"...","val":N} (ATOMAR, ein Push)
    Broker->>Plate: (Subscription)
    Plate->>Plate: Dropdown zeigt sofort den<br/>korrekten Eintrag, kein Zwischenbild
    Plate->>User: Anzeige aktualisiert
```

**Warum atomar wichtig ist:** ein früherer Ansatz schrieb `options` und `val` als zwei
getrennte MQTT-Publishes — die Firmware setzt bei jedem `options`-Update die
Dropdown-Auswahl intern kurz auf Index 0 zurück, bevor der zweite Publish sie korrigiert,
was als sichtbares Aufblitzen des falschen Eintrags auffiel. Die kombinierte `jsonl`-Property
verarbeitet beide Werte in einem Firmware-Durchlauf ohne Netzwerk-Zwischenschritt.

## 4. Datenfluss-Beispiel: Ventil im Web-Interface schalten

Zeigt die Besonderheit aus Diagramm 1 konkret: der Browser nimmt beim Schalten **keinen
Umweg über das Gerät**.

```mermaid
sequenceDiagram
    participant User
    participant Browser as Browser (WebIF)
    participant Broker as MQTT-Broker
    participant FW as Geraete-Firmware
    participant HA as Home Assistant

    User->>Browser: Tippt Ventilkachel V1
    Browser->>Broker: gartenwasser/V1/cmd = ON<br/>(MQTT-over-WebSocket, DIREKT)
    Note over Browser,Broker: Das Geraet ist hier NICHT beteiligt -<br/>der Browser spricht direkt mit dem Broker
    Broker->>FW: (Subscription)
    FW->>FW: schaltet Relais V1
    FW->>Broker: gartenwasser/V1/state = ON (retained)
    Broker->>Browser: Live-Update im WebIF
    Broker->>HA: Live-Update: switch.gartenbewasserung_ventil_1
    HA->>HA: Dashboard- und Plate-Bindings aktualisieren sich
```

Derselbe Ablauf gilt spiegelbildlich für jede Aktion, die am Dashboard oder am Plate
ausgelöst wird — alle drei Oberflächen sind gleichrangige MQTT-Teilnehmer, keine ist die
"Quelle der Wahrheit" gegenüber den anderen. Die Firmware selbst ist die einzige Instanz,
die tatsächlich validiert und persistiert.

## 5. Weiterführend

- Vollständige Entity-/Objekt-Referenz: `docs/manual/usermanual.md`, Kapitel 8 (HA) und 9 (Plate).
- Vollständiger MQTT-Topic-Baum: `docs/manual/usermanual.md`, Kapitel 7.
- C++-Firmware-Struktur (Klassen/Aufrufgraphen): [Code-Struktur durchsuchen](https://pfannex.github.io/Gartenwasser/doxygen/html/index.html) (siehe auch `docs/development.md`).
- Technische Stolpersteine/Lessons Learned zu HA/openHASP (Reload-Verhalten, Ghost-State,
  Freeze-Properties u. a.): `docs/homeassistant/README.md`.
