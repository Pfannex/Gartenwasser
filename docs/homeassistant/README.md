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
| **Mushroom** | `piitaya/lovelace-mushroom` | `mushroom-template-card` auf der Programme- und Konfigurationsseite (Icon + Text in einer Box) — reine Bordmittel-Karten (`tile`, `markdown`) konnten das nicht: eine `tile`-Karte ist immer als Ganzes die Tap-Fläche und lässt sich nicht mit fremden Entities kombinieren, eine `markdown`-Karte filtert Inline-`style`-Attribute per DOMPurify-Sanitizing komplett raus (kein Grid/keine Farben rendern). Auf der Status-Seite inzwischen komplett durch Bubble Card ersetzt (siehe unten). |
| **card_mod** | `thomasloven/lovelace-card-mod` | CSS-Override für die Icon-Größe der Mushroom-Karte (`--icon-size`) — Mushroom selbst bietet dafür keine YAML-Option. |
| **stack-in-card** | `custom-cards/stack-in-card` | Fasst mehrere Karten (z. B. Programm-Zeile + Aktions-Buttons) zu einer optisch nahtlosen Kachel mit gemeinsamem Rahmen zusammen — ein reiner `horizontal-stack` (Bordmittel) lässt jede Kind-Karte mit eigenem Schatten/Abstand stehen. |
| **timer-bar-card** | `rianadon/timer-bar-card` | Fortschrittsbalken auf der Programme-Seite (Mushroom-Variante) — berechnet die Restzeit selbst aus Client-Uhrzeit + `duration`, ohne durchgehend auf die nicht-retained `time/remaining`-Topics angewiesen zu sein. Auf der Status-Seite durch einen selbstgebauten CSS-Gradient-Fill in Bubble Card ersetzt (siehe `docs/Log.md` — nativer Slider-Mechanismus verursachte einen Interaktions-Bug). |
| **Bubble Card** | `Clooos/Bubble-Card` | Auf der Status-Seite die alleinige, vollstaendige Umsetzung (Start/Stop, Hauptventil, Ventilzeilen inkl. Fuellstand-Balken als CSS-Gradient) — ersetzt dort die urspruengliche Mushroom-Variante komplett. Auf der Programme-Seite weiterhin als Alternativ-Nachbau (`card_type: button` + `sub_button`) parallel zur Mushroom-Variante vorhanden, zu Vergleichszwecken auf Nutzerwunsch. Unterstützt keine Jinja-Templates, nur eigene JS-Templates (`${...}`) in `styles:`. |
| **Bubble Card Tools** (Integration, kein Frontend-Repo) | `Clooos/Bubble-Card-Tools` | Backend für Bubble Cards Module Store/Editor — zusätzlich zur Karte über HACS heruntergeladen und unter Einstellungen → Geräte & Dienste eingerichtet, nach HA-Neustart. |

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

Seit 2026-08-24 als HA **Package** organisiert (siehe [Configuration packages](https://www.home-assistant.io/docs/configuration/packages/))
statt als einzelne Direkt-Includes in der Haupt-`configuration.yaml` — bündelt alle
Gartenwasser-Domains (Helper, Skripte, Automation, MQTT, Template) an einem Ort, statt
über mehrere domain-weite Dateien verstreut zu sein, in denen sich Gartenwasser sonst
mit fremden, unrelated Konfigurationen mischen würde.

| Datei | Ziel in deiner HA-Konfiguration |
|---|---|
| `input_number.yaml` | `configurations/gartenwasser/input_number.yaml` |
| `input_boolean.yaml` | `configurations/gartenwasser/input_boolean.yaml` |
| `input_select.yaml` | `configurations/gartenwasser/input_select.yaml` |
| `input_text.yaml` | `configurations/gartenwasser/input_text.yaml` |
| `input_datetime.yaml` | `configurations/gartenwasser/input_datetime.yaml` |
| `automations.yaml` | `configurations/gartenwasser/automations.yaml` |
| `script.yaml` | `configurations/gartenwasser/scripts.yaml` |
| `mqtt.yaml` | `configurations/gartenwasser/mqtt.yaml` (kompletter Inhalt, kein Anhängen mehr nötig — eigene Datei statt gemeinsamer `mqtt.yaml`) |
| `template.yaml` | `configurations/gartenwasser/template.yaml` |
| `dashboard-programme.yaml` | Als neues Dashboard einfügen (siehe Kommentar in der Datei) |

## Einrichtungsschritte

1. Die neun Dateien oben 1:1 (Dateiname bleibt gleich, `script.yaml` wird zu
   `scripts.yaml`) nach `configurations/gartenwasser/` kopieren.
2. Neue Datei `configurations/packages/gartenwasser.yaml` anlegen, die alle neun
   Domains bündelt:
   ```yaml
   input_number: !include ../gartenwasser/input_number.yaml
   input_boolean: !include ../gartenwasser/input_boolean.yaml
   input_select: !include ../gartenwasser/input_select.yaml
   input_text: !include ../gartenwasser/input_text.yaml
   input_datetime: !include ../gartenwasser/input_datetime.yaml
   automation: !include ../gartenwasser/automations.yaml
   script: !include ../gartenwasser/scripts.yaml
   mqtt: !include ../gartenwasser/mqtt.yaml
   template: !include ../gartenwasser/template.yaml
   ```
   Pfade sind relativ zur Package-Datei selbst (empirisch verifiziert per
   `POST /api/config/core/check_config`, nicht relativ zum Config-Root).
3. In der Haupt-`configuration.yaml` **eine einzige neue Zeile** im `homeassistant:`-Block
   ergänzen (keine der neun Domains braucht mehr eine eigene Include-Zeile):
   ```yaml
   homeassistant:
     packages: !include_dir_named configurations/packages
   ```
4. Vor dem Neustart validieren, ohne etwas zu riskieren: `POST /api/config/core/check_config`
   (Bearer-Token aus `localStorage.hassTokens`) meldet `"result": "valid"` bei korrekter
   Konfiguration, ohne die laufende Instanz anzufassen.
5. **Home Assistant komplett neu starten** (Einstellungen → System → Neu starten, NICHT
   nur „Alle YAML-Konfigurationen neu laden“) — ein brandneuer Top-Level-Schlüssel wie
   `packages:`, der vorher noch nie in der `configuration.yaml` stand, wird von einem
   reinen YAML-/Core-Config-Reload nachweislich NICHT aktiviert (getestet: Test-Entity
   blieb nach `reload_core_config` unauffindbar), erst ein echter Neustart richtet die
   Package-Einbindung ein. Bereits bestehende, im Package gebündelte Domains
   (`input_number`/`script`/`automation`/...) reloaden sich danach einzeln wieder ohne
   Neustart zuverlässig.
6. Nach dem Neustart prüfen: alle Gartenwasser-Entities (`input_number.gartenwasser_*`,
   `sensor.gartenwasser_*`, `script.gartenwasser_*`, `automation.gartenwasser_*` usw.)
   sollten wieder unter denselben Entity-IDs wie vorher erreichbar sein — Package-Merge
   ändert nichts an Entity-IDs/unique_ids, nur am Speicherort der YAML-Quelle.
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
