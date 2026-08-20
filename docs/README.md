# Dokumentation — Gartenwasser

- [requirements.md](requirements.md) — Funktionsbeschreibung, MQTT-Topic-Struktur, Architektur, offene Punkte
- [spec/](spec/) — Phasen-Specs der Implementierung (Ziel, Umsetzung, MQTT-Topics, Test je Phase)
- [testing.md](testing.md) — zentrale, tabellarische Testergebnis-Übersicht (Prüfpunkt/Test/Ergebnis/Bewertung)

## Phasen-Übersicht

Tabellenreihenfolge = geplante Bearbeitungsreihenfolge, nicht die Phasennummer (die bleibt als stabiler Datei-Bezeichner erhalten). Priorisierung (2026-08-15, erneut bestätigt 2026-08-17): Phase 10 (Home Assistant, externe Integration) bewusst ans Ende gestellt — zuerst alles geräteintern (Phasen 11–15) fertigstellen. Das Web-Interface (Phasen 16–21) wurde nachträglich davor eingeordnet, nicht danach wie ursprünglich geplant — Auslöser: die Touch-UI-Zeitplanbedienung wurde verworfen (Display zu klein), Zeitplan-Bearbeitung soll stattdessen komfortabel im Browser laufen. Siehe `requirements.md`, Entscheidungshistorie.

| Phase | Thema | Status |
|---|---|---|
| [00](spec/00-grundgeruest.md) | Grundgerüst | ✅ |
| [01](spec/01-wlan.md) | WLAN-Verbindung | ✅ |
| [02](spec/02-mqtt-grundgeruest.md) | MQTT-Grundgerüst + `availability`/LWT | ✅ |
| [03](spec/03-valve-controller.md) | ValveController (MCP23017-Kapselung) | ✅ |
| [04](spec/04-ventile-mqtt.md) | Ventile per MQTT (`cmd`/`state`, V0-Kopplung) | ✅ |
| [05](spec/05-laufzeit-restlaufzeit.md) | Laufzeit & Restlaufzeit je Ventil | ✅ |
| [06](spec/06-automatik-flag.md) | Automatik-Flag je Ventil | ✅ |
| [07](spec/07-automatik-sequenz.md) | Automatik-Sequenz (`main/cmd`, `AutomaticController`) | ✅ |
| [08](spec/08-diagnostics.md) | Diagnostics (`i2cStatus`, `lastError`) | ✅ |
| [09](spec/09-alias.md) | Alias je Ventil (inkl. V0, `set`, Persistenz) | ✅ |
| [11](spec/11-sammelbefehle.md) | Konfiguration per JSON (`main/config/set`/`state`) | ✅ |
| [12](spec/12-aufraeumen.md) | Aufräumen/Refactoring | ✅ (Regressionstest: alle 10 Checklistenpunkte bestanden) |
| [13](spec/13-touch-ui.md) | Touch-UI (Automatik-Toggle & Statusanzeige) | ✅ |
| [14](spec/14-programme.md) | Bewässerungsprogramme (`main/program/cmd`/`state`) | ✅ |
| [15](spec/15-wochenplan.md) | Zeitplan / Scheduler (Tages- & Wochenplan auf Programme + `AutomaticController`) | ✅ |
| [16](spec/16-webif-fundament.md) | Web-Interface: Fundament & Architekturentscheidung | ✅ |
| [17](spec/17-webif-dashboard.md) | Web-Interface: Status-Dashboard (read-only) | ✅ |
| [18](spec/18-webif-konfiguration.md) | Web-Interface: Konfiguration bearbeiten | ✅ |
| [19](spec/19-webif-programme.md) | Web-Interface: Programme verwalten | ✅ |
| [20](spec/20-webif-zeitplan.md) | Web-Interface: Zeitplan verwalten | ✅ |
| [21](spec/21-webif-ota.md) | Web-Interface: Firmware-Update (OTA) | ✅ |
| [10](spec/10-ha-discovery.md) | Home Assistant MQTT-Discovery | ✅ |

Damit sind alle geplanten Phasen abgeschlossen. Verbleibend nur noch (siehe `docs/Log.md`, „Offene Punkte“): das zurückgestellte WebIF-Verbindungsproblem (braucht das iPhone der Ehefrau zum Nachtesten) und die humorvoll auf „V2.0“ vertagte 16-Ventile-Erweiterung.
