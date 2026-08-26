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

## Wo die eigentlichen Dateien liegen

Dieser Ordner enthält seit 2026-08-24 nur noch diese Erklärung — die tatsächlichen
Konfigurationsdaten liegen im Repo-Root unter [`HomeAssistant/`](../../HomeAssistant/),
als 1:1-Spiegel der Verzeichnisstruktur, die auch auf der echten HA-Instanz liegt (statt
als "hier reinkopieren"-Anleitung mit Zieltabelle wie zuvor). Wer die Konfiguration neu
aufsetzt, kopiert `HomeAssistant/` einfach direkt in den Config-Root der eigenen
HA-Instanz.

Organisiert als HA **Package** (siehe [Configuration packages](https://www.home-assistant.io/docs/configuration/packages/))
statt als einzelne Direkt-Includes in der Haupt-`configuration.yaml` — bündelt alle
Gartenwasser-Domains (Helper, Skripte, Automation, MQTT, Template) an einem Ort, statt
über mehrere domain-weite Dateien verstreut zu sein, in denen sich Gartenwasser sonst mit
fremden, unrelated Konfigurationen mischen würde:

```
HomeAssistant/
  configuration.yaml              # Root-Config, referenziert alles Weitere per !include
  customize.yaml
  configurations/
    packages/
      gartenwasser.yaml           # buendelt die 9 Domains unten in die echten Top-Level-Keys
      plate_wz.yaml                # buendelt die plate_wz-Domains (aktuell nur automation)
    gartenwasser/                 # die rohen Domain-Dateien, vom Package eingebunden
      input_number.yaml
      input_boolean.yaml
      input_select.yaml
      input_text.yaml
      input_datetime.yaml
      automations.yaml
      scripts.yaml
      mqtt.yaml
      template.yaml
    plates/                       # openHASP-Touchpanels, ein Unterordner je Geraet
      openhasp.yaml                # duenner Manifest: Slug -> Geraete-Unterordner
      plate_wz/
        openhasp.yaml               # objects: Property-Bindungen/Events fuer die Status-Seite
        automations.yaml            # plate-spezifische Automationen (z.B. Idle-Dimmen)
        device/                    # Backup des tatsaechlichen Geraete-Stands (read-only Referenz)
          pages.jsonl
          config.json              # Passwoerter maskiert ("********"), NUR Boot-Zeit-Snapshot -
                                    # laufende Config-Aenderungen ueber /api/config/<x>/ zeigen
                                    # sich hier erst nach einem echten Neustart
          play.png / stop.png       # eigene PNG-Icons fuer den Start/Stop-Button
  dashboards/
    gartenwasser.yaml             # das Lovelace-Dashboard (YAML-Modus)
  themes/
```

**Persistente Geraete-Einstellungen (z. B. Idle-Timeouts, Backlight-Pin) NICHT per MQTT
`config/<submodul>` setzen** - kam im Test nachweislich nicht an (per MQTT-Mitschnitt bestaetigt,
trotz gegenteiliger Doku-Aussage). Der tatsaechlich funktionierende Weg (reverse-engineered aus dem
Web-Editor, `static/main.js`, Funktion `submitOldConfig`): `GET /api/config/<submodul>/` liefert das
aktuelle Objekt, `POST /api/config/<submodul>/` mit dem KOMPLETTEN (nicht nur geaenderten) Objekt als
JSON-Body speichert es persistent. Nach einer Aenderung zeigt `GET /api/config/<submodul>/` sofort
den neuen Stand, `config.json` (Boot-Snapshot) aber erst nach einem echten Neustart.

**`hasp.color2` (Submodul `hasp`, `GET`/`POST /api/config/hasp/`) ist die THEME-Sekundaerfarbe und
wird von LVGL geraeteweit fuers Pressed/Checked-Feedback interaktiver Elemente verwendet** - lokale
Objekt-Style-Overrides (`bg_color02`/`bg_color03`, siehe Styling-Suffixe unten) koennen das fuer
manche Zustandskombinationen NICHT ueberschreiben (per Diagnose-Test verifiziert: `color2` testweise
auf eine Kontrastfarbe gesetzt, der Press-Flash uebernahm sie direkt, obwohl lokale Overrides gesetzt
waren - siehe `docs/Log.md`). Fuer plate_wz aktuell auf `#e53935` (unser Rot) gesetzt, damit der
Press-Flash beim Einschalten eines Ventils nicht kontrastiert. Wirkt sich auf ALLE Seiten des
Geraets aus, nicht nur eine einzelne - bei einer neuen Farbwahl alle Seiten pruefen.

**openHASP-Styling-Suffixe** (fuer `bg_color`/`text_color`/etc. an einzelnen Objekten): zweistelliger
Zahlen-Suffix, 1. Ziffer = Teil (0 = Hauptteil, andere Werte fuer Sub-Widgets wie btnmatrix-Items),
2. Ziffer = Zustand (0=Standard, 1=umgeschaltet/checked, 2=gedrueckt nicht umgeschaltet, 3=gedrueckt
UND umgeschaltet, 4=deaktiviert nicht umgeschaltet, 5=deaktiviert umgeschaltet) - z. B. `bg_color03`
= Hauptteil, gedrueckt+umgeschaltet. Aus der Firmware-Quelle (`src/hasp/hasp_attribute.cpp`,
`hasp_attribute_get_part_state_new()`) verifiziert, nicht nur aus Doku/Community-Posts uebernommen.

**openHASP-Integration:** hybrid, nicht rein YAML-gesteuert wie urspruenglich angenommen. Ein
Plate wird per echter MQTT-Discovery automatisch als Config-Entry angelegt (`hasp/discovery/<hwid>`,
sichtbar unter Einstellungen → Geraete & Dienste → openHASP), sobald es online ist. Der YAML-Block
unter `openhasp: <slug>:` liefert nur die ERGAENZENDE `objects`-Konfiguration (Property-Bindungen,
Event-Handler) zu diesem bereits per Discovery angelegten Entry - ohne den YAML-Eintrag wirft die
Integration bei jeder MQTT-Nachricht vom Geraet einen Fehler ("No YAML configuration for `<slug>`,
please create an entry under 'openhasp' with the slug: `<slug>`"), auch wenn das Geraet laengst
online ist. Minimal reicht `<slug>: {objects: []}` zum Stillhalten des Fehlers.

**Wichtig — Aenderungen an `openhasp: <slug>: objects:` brauchen den gezielten Entry-Reload, NICHT
"Alle YAML-Konfigurationen neu laden":** empirisch getestet (2026-08-25, `docs/Log.md`) - ein
Property testweise auf einen fixen Marker-Wert gesetzt, `homeassistant.reload_all` aufgerufen,
Marker blieb wirkungslos; derselbe gezielte Reload (Einstellungen → Geraete & Dienste → openHASP →
`<slug>` → Neu laden, bzw. `POST /api/config/config_entries/entry/<entry_id>/reload`) zog die
Aenderung sofort. `reload_all` deckt nur eine feste Liste von Domains mit eigenem
`<domain>.reload`-Service ab (automation/script/scene/input_*/...) - Config-Entry-Integrationen wie
openHASP sind dort nicht dabei.

## Einrichtungsschritte

1. Inhalt von `HomeAssistant/` in den Config-Root der eigenen HA-Instanz kopieren
   (bzw. mergen, falls dort schon andere, unrelated Konfiguration liegt — `packages:`,
   `scene:`, `openhasp:` und `lovelace.dashboards.gartenwasser-dashboard` muessen dann von
   Hand in die eigene `configuration.yaml` uebernommen werden statt die Datei zu
   ueberschreiben).
2. Vor dem Neustart validieren, ohne etwas zu riskieren: `POST /api/config/core/check_config`
   (Bearer-Token aus `localStorage.hassTokens`) meldet `"result": "valid"` bei korrekter
   Konfiguration, ohne die laufende Instanz anzufassen.
3. **Home Assistant komplett neu starten** (Einstellungen → System → Neu starten, NICHT
   nur „Alle YAML-Konfigurationen neu laden“) — ein brandneuer Top-Level-Schlüssel wie
   `packages:`, der vorher noch nie in der `configuration.yaml` stand, wird von einem
   reinen YAML-/Core-Config-Reload nachweislich NICHT aktiviert (getestet: Test-Entity
   blieb nach `reload_core_config` unauffindbar), erst ein echter Neustart richtet die
   Package-Einbindung ein. Bereits bestehende, im Package gebündelte Domains
   (`input_number`/`script`/`automation`/...) reloaden sich danach einzeln wieder ohne
   Neustart zuverlässig.
4. Nach dem Neustart prüfen: alle Gartenwasser-Entities (`input_number.gartenwasser_*`,
   `sensor.gartenwasser_*`, `script.gartenwasser_*`, `automation.gartenwasser_*` usw.)
   sollten unter denselben Entity-IDs erreichbar sein wie in einer bereits laufenden
   Installation.
5. Entity-IDs im Dashboard vorher gegen die eigene Installation prüfen (Einstellungen →
   Geräte & Dienste → Entitäten) — `dashboards/gartenwasser.yaml` enthält bereits die per
   Entity-Registry verifizierten IDs (Präfix `gartenbewasserung_`), bei Namenskollisionen
   kann HA aber einen abweichenden Suffix („_2") vergeben.

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
