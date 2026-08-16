# Dokumentation — Gartenwasser

- [requirements.md](requirements.md) — Funktionsbeschreibung, MQTT-Topic-Struktur, Architektur, offene Punkte
- [spec/](spec/) — Phasen-Specs der Implementierung (Ziel, Umsetzung, MQTT-Topics, Test je Phase)
- [testing.md](testing.md) — zentrale, tabellarische Testergebnis-Übersicht (Prüfpunkt/Test/Ergebnis/Bewertung)

## Phasen-Übersicht

Tabellenreihenfolge = geplante Bearbeitungsreihenfolge, nicht die Phasennummer (die bleibt als stabiler Datei-Bezeichner erhalten). Priorisierung (2026-08-15): Phase 10 (Home Assistant, externe Integration) bewusst ans Ende gestellt — zuerst alles geräteintern (Phasen 11–15) fertigstellen, siehe `requirements.md`, Entscheidungshistorie.

| Phase | Thema | Status |
|---|---|---|
| [00](spec/00-grundgeruest.md) | Grundgerüst | ✅ |
| [01](spec/01-wlan.md) | WLAN-Verbindung | ✅ |
| [02](spec/02-mqtt-grundgeruest.md) | MQTT-Grundgerüst + `availability`/LWT | ✅ |
| [03](spec/03-valve-controller.md) | ValveController (MCP23017-Kapselung) | ✅ |
| [04](spec/04-ventile-mqtt.md) | Ventile per MQTT (`cmd`/`state`, V0-Kopplung) | ✅ |
| [05](spec/05-laufzeit-restlaufzeit.md) | Laufzeit & Restlaufzeit je Ventil | ✅ |
| [06](spec/06-automatik-flag.md) | Automatik-Flag je Ventil | ✅ |
| [07](spec/07-automatik-sequenz.md) | Automatik-Sequenz (`main/cmd`, Sequencer) | ✅ |
| [08](spec/08-diagnostics.md) | Diagnostics (`i2cStatus`, `lastError`) | ✅ |
| [09](spec/09-alias.md) | Alias je Ventil (inkl. V0, `set`, Persistenz) | ✅ |
| [11](spec/11-sammelbefehle.md) | Konfiguration per JSON (`main/config/set`/`state`) | ✅ |
| [12](spec/12-aufraeumen.md) | Aufräumen/Refactoring | ✅ (Regressionstest: alle 10 Checklistenpunkte bestanden) |
| [13](spec/13-touch-ui.md) | Touch-UI (Automatik-Toggle & Statusanzeige) | ✅ |
| [14](spec/14-programme.md) | Bewässerungsprogramme (`main/program/cmd`/`state`) | ✅ |
| [15](spec/15-wochenplan.md) | Zeitplan / Scheduler (Tages- & Wochenplan auf Programme + Sequencer) | 📋 (Backlog, grob skizziert) |
| [10](spec/10-ha-discovery.md) | Home Assistant MQTT-Discovery | 📋 (zurückgestellt — externe Integration, ganz zum Schluss) |
