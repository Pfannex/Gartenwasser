# Dokumentation — Gartenwasser

- [requirements.md](requirements.md) — Funktionsbeschreibung, MQTT-Topic-Struktur, Architektur, offene Punkte
- [spec/](spec/) — Phasen-Specs der Implementierung (Ziel, Umsetzung, MQTT-Topics, Test je Phase)

## Phasen-Übersicht

| Phase | Thema | Status |
|---|---|---|
| [00](spec/00-grundgeruest.md) | Grundgerüst | ✅ |
| [01](spec/01-wlan.md) | WLAN-Verbindung | ✅ |
| [02](spec/02-mqtt-grundgeruest.md) | MQTT-Grundgerüst + `availability`/LWT | ✅ |
| [03](spec/03-valve-controller.md) | ValveController (MCP23017-Kapselung) | ✅ |
| [04](spec/04-ventile-mqtt.md) | Ventile per MQTT (`cmd`/`state`, V0-Kopplung) | ✅ |
| [05](spec/05-laufzeit-restlaufzeit.md) | Laufzeit & Restlaufzeit je Ventil | ✅ |
| [06](spec/06-automatik-flag.md) | Automatik-Flag je Ventil | 📋 |
| [07](spec/07-automatik-sequenz.md) | Automatik-Sequenz (`main/cmd`, Sequencer) | 📋 |
| [08](spec/08-diagnostics.md) | Diagnostics (`i2cStatus`, `lastError`) | 📋 |
| [09](spec/09-alias.md) | Alias je Ventil (inkl. `set`, Persistenz) | 📋 |
| [10](spec/10-ha-discovery.md) | Home Assistant MQTT-Discovery | 📋 |
| [11](spec/11-sammelbefehle.md) | Sammel-Befehle `main/time/set`, `main/auto/set` (JSON) | 📋 |
| [12](spec/12-aufraeumen.md) | Aufräumen/Refactoring | 📋 (teils vorgezogen) |
| [13](spec/13-touch-ui.md) | Touch-UI (Automatik-Toggle & Statusanzeige) | 📋 |
