# Home-Assistant-Integration — manuell mitgelieferte Konfiguration

Ergänzt die automatische MQTT-Discovery (Phase 10, 26 Entities — Ventile, Automatik,
Diagnose, siehe `docs/spec/10-ha-discovery.md`) um das, was Discovery allein nicht
abdeckt: eine Programme-Auswahlliste mit automatisch synchronisierter Optionsliste
(Phase 10.1) sowie ein eigenes Status-Dashboard, das die WebIF-Statusseite
(`docs/manual/images/webif-status.webp`) im Rahmen der verfügbaren Entities nachbildet
(Phase 10.3).

**Voraussetzung:** Phase 10 (Discovery) läuft bereits — die 26 Basis-Entities müssen
in Home Assistant sichtbar sein, bevor diese Ergänzung Sinn ergibt (sie referenziert
z. B. `switch.gartenbewasserung_automatik_sequenz` im Dashboard).

## Abhängigkeiten (Custom Cards über HACS)

Das Status-Dashboard (`dashboard-programme.yaml`) nutzt zwei Custom Cards, die HACS
(Home Assistant Community Store) voraussetzen. Beide über HACS → Frontend → „+
Repositories erkunden & herunterladen" installieren, danach Browser-Cache leeren:

| Card | Repository | Wofür |
|---|---|---|
| **Mushroom** | `piitaya/lovelace-mushroom` | `mushroom-template-card` für die Start/Stop-Kachel (Icon + Programmname in einer Box) — reine Bordmittel-Karten (`tile`, `markdown`) konnten das nicht: eine `tile`-Karte ist immer als Ganzes die Tap-Fläche und lässt sich nicht mit fremden Entities kombinieren, eine `markdown`-Karte filtert Inline-`style`-Attribute per DOMPurify-Sanitizing komplett raus (kein Grid/keine Farben rendern). |
| **card_mod** | `thomasloven/lovelace-card-mod` | CSS-Override für die Icon-Größe der Mushroom-Karte (`--icon-size`) — Mushroom selbst bietet dafür keine YAML-Option. |
| **stack-in-card** | `custom-cards/stack-in-card` | Fasst mehrere Karten (z. B. Start/Stop-Button + Programm-Anzeige) zu einer optisch nahtlosen Kachel mit gemeinsamem Rahmen zusammen — ein reiner `horizontal-stack` (Bordmittel) lässt jede Kind-Karte mit eigenem Schatten/Abstand stehen. |
| **timer-bar-card** | `rianadon/timer-bar-card` | Fortschrittsbalken für Ventil-Laufzeit + Gesamtlaufzeit der Sequenz — berechnet die Restzeit selbst aus Client-Uhrzeit + `duration`, ohne durchgehend auf die nicht-retained `time/remaining`-Topics angewiesen zu sein (Ausnahme: der Gesamtlaufzeit-Balken nutzt `main/remainingTotal` zusätzlich als `remain_time`, siehe `dashboard-programme.yaml`). |

Zusätzlich in der Test-Instanz installiert, aber von dieser Config **nicht** referenziert (nicht
nötig für den Nachbau, nur zur Vollständigkeit dokumentiert — vermutlich aus der verlorenen
Copilot-CLI-Session, siehe `docs/Log.md`):

| Card | Repository | Status |
|---|---|---|
| custom-card-features | `Nerwyn/custom-card-features` | installiert, ungenutzt |
| card-mod-studio | `DerTrolli/card-mod-studio` | installiert, ungenutzt |

Grund für die Wahl "Custom Card statt reinem YAML": beide o.g. Board-Mittel-Ansätze
sind an dieser Stelle nachweislich gescheitert (siehe `docs/Log.md`, Nachtrag
Phase 10.3) — Mushroom+card_mod ist der Community-Standardweg für zusammengesetzte,
gestylte Dashboard-Kacheln in Home Assistant.

## Dateien in diesem Ordner

| Datei | Ziel in deiner HA-Konfiguration |
|---|---|
| `mqtt.yaml` | Inhalt an `configurations/mqtt.yaml` anhängen (unter den bestehenden `light:`-Block) |
| `input_select.yaml` | Als `configurations/input_select.yaml` ablegen (neu) |
| `template.yaml` | Als `configurations/template.yaml` ablegen (neu) |
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
3. **`template.yaml`** (neu): Inhalt als `configurations/template.yaml` speichern. In der
   Haupt-`configuration.yaml` einen **neuen** Include ergänzen — `template` gibt es bei
   dir aktuell noch nicht:
   ```yaml
   template: !include configurations/template.yaml
   ```
4. **`automations.yaml`**: die drei Automationen an `configurations/automations.yaml`
   anhängen (bestehende Automationen bleiben unverändert).
5. **Home Assistant komplett neu starten** (Einstellungen → System → Neu starten, NICHT
   nur „Alle YAML-Konfigurationen neu laden“) — ein brandneuer Top-Level-Schlüssel wie
   `template:`, der vorher noch nie in deiner `configuration.yaml` stand, wird von einem
   reinen YAML-Reload nicht wirklich initialisiert (die Entities bleiben dauerhaft
   "unknown"/nicht auffindbar), erst ein echter Neustart richtet die Integration ein.
   Bereits bestehende Domains (`mqtt`/`input_select`/`automation`) reloaden dagegen ohne
   Neustart zuverlässig.
6. Prüfen: `input_select.gartenwasser_programm` sollte nach dem Neustart automatisch
   mit den aktuellen Programmnamen befüllt sein (Automation „Gartenwasser: Programme-
   Liste synchronisieren“ läuft beim ersten `main/programs/state`-Update).
7. **`dashboard-programme.yaml`**: entweder über Einstellungen → Dashboards → „+
   Dashboard hinzufügen“ → „Neues Dashboard aus YAML erstellen“ einfügen, oder als
   Datei ablegen (z. B. `dashboards/gartenwasser.yaml`) und in `configuration.yaml`
   registrieren:
   ```yaml
   lovelace:
     mode: storage
     dashboards:
       gartenwasser-dashboard:   # url_path braucht zwingend einen Bindestrich, sonst
         mode: yaml              # "Invalid config for 'lovelace'" beim Config-Check
         title: Gartenwasser
         icon: mdi:sprinkler-variant
         show_in_sidebar: true
         filename: dashboards/gartenwasser.yaml
   ```
   Vorher die Entity-IDs gegen deine tatsächliche Installation prüfen (Einstellungen →
   Geräte & Dienste → Entitäten) — die Datei enthält bereits die per Entity-Registry
   verifizierten IDs (Präfix `gartenbewasserung_`), bei Namenskollisionen kann HA aber
   einen abweichenden Suffix („_2") vergeben.

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
