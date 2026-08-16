# MQTT-Testskripte

Automatisierte Tests gegen die echte Hardware (kein Mock/Simulator) — Ergebnisse siehe [`docs/testing.md`](../../docs/testing.md).

## Voraussetzungen

```
pip install -r requirements.txt
```

- Board mit aktueller Firmware geflasht, WLAN + MQTT verbunden (`gartenwasser/availability` = `online`).
- Broker-Adresse und Topic-Präfix sind oben in jedem Skript als Konstanten (`BROKER`, `PORT`, `PREFIX`) hinterlegt — bei Bedarf anpassen (aktuell `192.168.1.123:1883`, `gartenwasser`).
- `test_regression_full.py` braucht zusätzlich eine USB-Verbindung zum Board für den Persistenz-Test (`SERIAL_PORT`, aktuell `COM8`) — löst darüber einen echten Hardware-Reset per `esptool` aus (kein Reflash, nur Reset-Pin-Puls).

## Skripte

- **`test_phase14.py`** — Bewässerungsprogramme (`main/program/cmd`/`state`, `main/programs/set`/`state`): Bulk-Replace, Index-Auswahl, ungültiger Index, Teilmengen-Semantik. Laufzeit ca. 30 s.
- **`test_regression_full.py`** — Gesamt-Regressionstest nach der Checkliste in `docs/spec/12-aufraeumen.md` (Ventile, Laufzeit inkl. echtem 60s-Zeitablauf, Automatik-Sequenz, Alias, Config-JSON, Persistenz über echten Reboot). Laufzeit ca. 3 Minuten. Sichert vor dem Lauf den aktuellen `config`/`programs`-Stand und stellt ihn am Ende wieder her.

```
python test_phase14.py
python test_regression_full.py
```

## Achtung

Beide Skripte schalten echte Ventile (inkl. `V0`/Hauptventil) für kurze Zeit ein (max. 1 Minute je Ventil) und ändern reale Konfigurationswerte auf dem Gerät (werden am Ende zurückgesetzt). `test_regression_full.py` löst zusätzlich einen echten Neustart des Boards aus. Nicht gegen ein produktiv im Einsatz befindliches Gerät mit angeschlossenem Wasserdruck laufen lassen, ohne sich der Konsequenzen bewusst zu sein.

## Nicht automatisiert

Zwei Punkte der Checkliste erfordern physischen Eingriff und werden von `test_regression_full.py` bewusst übersprungen (siehe Konsolen-Ausgabe `[MANUELL/OFFEN]`):

- I2C-Bus kurz trennen/wiederverbinden (Diagnostics-Fehlerfall).
- WLAN/MQTT-Verbindung trennen (Resilienz).

Vor dem produktiven Einsatz einmal manuell nachholen.
