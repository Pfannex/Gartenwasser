# Home-Assistant-Integration — manuell mitgelieferte Konfiguration

Ergänzt die automatische MQTT-Discovery (Phase 10, 26 Entities — Ventile, Automatik,
Diagnose, siehe `docs/spec/10-ha-discovery.md`) um das, was Discovery allein nicht
abdeckt: eine Programme-Auswahlliste mit automatisch synchronisierter Optionsliste
(Phase 10.1).

**Voraussetzung:** Phase 10 (Discovery) läuft bereits — die 26 Basis-Entities müssen
in Home Assistant sichtbar sein, bevor diese Ergänzung Sinn ergibt (sie referenziert
z. B. `switch.automatik_sequenz` im Dashboard).

## Dateien in diesem Ordner

| Datei | Ziel in deiner HA-Konfiguration |
|---|---|
| `mqtt.yaml` | Inhalt an `configurations/mqtt.yaml` anhängen (unter den bestehenden `light:`-Block) |
| `input_select.yaml` | Als `configurations/input_select.yaml` ablegen (neu) |
| `automations.yaml` | Inhalt an `configurations/automations.yaml` anhängen |
| `dashboard-programme.yaml` | Als neues Dashboard einfügen (siehe Kommentar in der Datei) |

## Einrichtungsschritte

1. **`mqtt.yaml`**: den `sensor:`-Block aus `docs/homeassistant/mqtt.yaml` unten an
   `configurations/mqtt.yaml` anhängen (bestehender `light:`-Block bleibt unverändert
   stehen).
2. **`input_select.yaml`** (neu): Inhalt als `configurations/input_select.yaml`
   speichern. In der Haupt-`configuration.yaml` einen **neuen** Include ergänzen —
   `input_select` gibt es bei dir aktuell noch nicht:
   ```yaml
   input_select: !include configurations/input_select.yaml
   ```
3. **`automations.yaml`**: die drei Automationen an `configurations/automations.yaml`
   anhängen (bestehende Automationen bleiben unverändert).
4. Home Assistant neu starten (Einstellungen → System → Neu starten) — YAML-Änderungen
   an `input_select`/`mqtt`/`automation` lassen sich zwar teils per „Config neu laden“
   übernehmen, ein kompletter Neustart ist aber der zuverlässigste Weg bei mehreren
   gleichzeitigen Änderungen.
5. Prüfen: `input_select.gartenwasser_programm` sollte nach dem Neustart automatisch
   mit den aktuellen Programmnamen befüllt sein (Automation „Gartenwasser: Programme-
   Liste synchronisieren“ läuft beim ersten `main/programs/state`-Update).
6. **`dashboard-programme.yaml`**: Einstellungen → Dashboards → „+ Dashboard
   hinzufügen“ → „Neues Dashboard aus YAML erstellen“, Inhalt einfügen. Vorher die
   Entity-IDs gegen deine tatsächliche Installation prüfen (Kommentar oben in der
   Datei) — HA leitet sie aus den Anzeigenamen ab, kann bei Namenskollisionen aber
   abweichen.

## Funktionsweise (kurz)

- `sensor.gartenwasser_programme` hält die rohe Programmliste (`main/programs/state`)
  als Attribute.
- Automation 1 aktualisiert bei jeder Programm-Änderung die Optionsliste des Dropdowns
  (`input_select.set_options`) — kein manuelles Nachpflegen nötig.
- Automation 2 hält das Dropdown synchron, wenn das Programm woanders gewählt wird
  (Touch-Display, WebIF, ein anderer MQTT-Client).
- Automation 3 publiziert bei einer Dropdown-Auswahl in HA den passenden Index an
  `gartenwasser/main/program/cmd` — genau wie ein manueller `mosquitto_pub`-Aufruf.

Kein Rückkopplungsrisiko: `input_select.select_option` löst nur bei tatsächlicher
Wertänderung einen `state`-Trigger aus, ein vom Gerät bestätigter, bereits aktiver
Wert erzeugt daher keine erneute Publikation.
