# Phase 10 — Home Assistant MQTT-Discovery

**Status:** ✅ Fertig (2026-08-19)

## Ziel

Automatische Geräte-/Entity-Erkennung in Home Assistant ohne manuelle YAML-Konfiguration.

## Voraussetzungen

- Phasen 2–9 (alle MQTT-Topics existieren bereits) ✅

## Umsetzung

- Neue Klasse `HaDiscovery` (`src/HaDiscovery.h/.cpp`):
  - Baut für jede Entity (siehe Mapping-Tabelle in `docs/requirements.md`, 26 Entities) eine Discovery-Config (JSON, via `ArduinoJson`, `StaticJsonDocument<512>` pro Entity — bewusst 26 kleine Einzel-Publishes statt einer gebündelten "device discovery"-Nachricht mit allen Entities, um nicht dasselbe Muster zu riskieren, das schon einmal einen `Guru Meditation Error` durch zu grosse JSON-Puffer auf dem Stack verursacht hat) und publiziert sie retained unter `homeassistant/<component>/gartenwasser/<object_id>/config`.
  - Gemeinsames `device`-Objekt (`identifiers: ["gartenwasser"]`, Name, Hersteller, `sw_version`) + `origin`-Objekt (Firmware-Herkunft) und `availability_topic`-Referenz auf `gartenwasser/availability` in jeder Config.
  - `MQTT.h` bekam dafür eine neue öffentliche `MQTT::publish()`-Funktion (dünner Wrapper um die bestehende private `publishAndLog()`) - `HaDiscovery` hat keinen direkten Zugriff auf den privaten `PubSubClient`.
  - Wird nach jedem erfolgreichen `MqttManager`-Connect erneut publiziert (Discovery-Configs sind retained, aber ein Re-Publish nach Reconnect stellt sicher, dass HA nach eigenem Neustart die Entities wiederfindet).
- **`V{n}_auto` als `binary_sensor` statt `switch`** (Abweichung vom ursprünglichen Plan, siehe `docs/requirements.md`-Nachtrag): passt zur MANUELL-Regel, verhindert eine überraschende, unsichtbare Programm-Abwahl durch einen HA-Schalter.

## Betroffene Dateien

- `src/HaDiscovery.h`, `src/HaDiscovery.cpp` (neu)
- `src/MQTT.h`/`.cpp` (neue öffentliche `publish()`-Funktion, Aufruf von `HaDiscovery::publishAll()` in `connectToBroker()`)

## Gefundener Bug: woertlicher Umlaut im Quelltext

`dev["name"] = "Gartenbewässerung";` (woertlicher Umlaut im Quelltext, Datei selbst korrekt UTF-8-kodiert, per Byte-Inspektion geprueft) fuehrte auf dem ESP32-C6-Toolchain zu einem einzelnen kaputten Byte in der publizierten Discovery-Config (ein U+FFFD-Ersatzzeichen statt "ä") - vermutlich eine Quelltext-Encoding-Fehlannahme des Compilers, nicht weiter root-gecausalt. **Fix**: expliziter Hex-Escape statt woertlichem Zeichen (`"Gartenbew\xc3\xa4sserung"` - `\xc3\xa4` ist die UTF-8-Bytefolge fuer "ä") - danach per MQTT-Snapshot byteweise verifiziert, korrekt. Fuer kuenftige Quelltextstellen mit deutschen Umlauten in String-Literalen relevant, die tatsaechlich zur Laufzeit ausgegeben werden (Kommentare sind unbetroffen).

## Test

1. Auf Hardware geflasht, per Python/paho-mqtt gegen den echten Broker verifiziert: alle 26 erwarteten `homeassistant/+/gartenwasser/+/config`-Topics kommen an (5 `switch`, 6 `binary_sensor`, 5 `number`, 10 `sensor`), keine fehlend/zusätzlich. ✅
2. Beispiel-Payload (`V1_auto`) inhaltlich geprüft: `state_topic`, `name`, `unique_id`, `availability_topic`, `device`- und `origin`-Objekt korrekt und vollständig, `device.name` ("Gartenbewässerung") nach dem Encoding-Fix byteweise korrekt (`\xc3\xa4`). ✅
3. ✅ Vom Nutzer gegen eine echte HA-Instanz bestätigt: Gerät „Gartenbewässerung“ erscheint mit allen Entities (Steuerung: Automatik-Sequenz, Ventil 1–5 + Laufzeit-Slider; Sensoren: Aktives Ventil, Hauptventil, I2C-Status, Letzter Fehler, Restzeit Sequenz, Ventil-Automatik/-Restlaufzeit je Ventil), Aktivität/Verlauf wird korrekt mitgeschrieben.

## Nachtrag: Phase 10.1 — Programme-Auswahlliste (2026-08-19)

Autodiscovery kann keine dynamische Optionsliste abbilden (HA-`select` bräuchte ein
Jinja-Template mit fest eingebackener Name→Index-Tabelle, die bei jeder Programmänderung
per Firmware-Reconnect neu veröffentlicht werden müsste — zu fragil). Stattdessen:
**Strategiewechsel** — zusätzlich zur automatischen Discovery liefert das Projekt ab jetzt
manuell einzubindende HA-YAML-Konfiguration mit (`docs/homeassistant/`), die der Nutzer
selbst in seine bestehende `configurations/`-Struktur übernimmt (kein automatischer
Rollout, kein Zugriff auf die HA-Instanz von hier aus möglich).

- `docs/homeassistant/mqtt.yaml` — neuer `sensor.gartenwasser_programme` (State: Anzahl
  Programme, Attribute: rohe `programs`-Liste + `activeProgram` via `json_attributes_topic`
  auf `main/programs/state`, ohne Template — HA übernimmt alle Top-Level-JSON-Felder
  automatisch als Attribute).
- `docs/homeassistant/input_select.yaml` — neuer Helper `input_select.gartenwasser_programm`
  (passt zum bestehenden `input_number.yaml`-Muster des Nutzers). Erfordert einen NEUEN
  Include in der Haupt-`configuration.yaml` (`input_select:` gab es dort noch nicht).
- `docs/homeassistant/automations.yaml` — drei Automationen für bidirektionale
  Synchronisierung: (1) Programmliste → Dropdown-Optionen (`input_select.set_options`,
  läuft bei jeder Änderung automatisch, kein manuelles Nachpflegen), (2) aktives Programm
  vom Gerät → Dropdown-Anzeige, (3) Dropdown-Auswahl → `main/program/cmd`. Kein
  Rückkopplungsrisiko, da `platform: state`-Trigger nur bei tatsächlicher Wertänderung
  feuert.
- `docs/homeassistant/dashboard-programme.yaml` — eigenes Lovelace-Dashboard (Programm-
  Auswahl, Start/Stop, Ventil-Übersicht, Diagnose). Entity-IDs aus den `name`-Feldern in
  `HaDiscovery.cpp` abgeleitet (HA-Slug-Konvention) — nicht gegen die echte Instanz
  verifizierbar, Nutzer muss vor Verwendung gegenprüfen (siehe Kommentar in der Datei).
- `docs/homeassistant/README.md` — Schritt-für-Schritt-Einbindungsanleitung.

**Noch offen**: Einbindung + Test durch den Nutzer in der echten HA-Instanz.
