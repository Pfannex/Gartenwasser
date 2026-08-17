# Phase 17 — Web-Interface: Status-Dashboard (read-only)

**Status:** ✅ Erledigt & getestet

## Ziel

Live-Übersicht im Browser: Ventilstatus (`V0`–`V5`), Automatik-Flags, laufende Sequenz samt Restlaufzeit, Diagnostics (`i2cStatus`/`lastError`) — inhaltliches Pendant zur Touch-UI-Hauptseite (Phase 13), aber am PC/Handy statt am Gerätedisplay. Bewusst **read-only**: erster echter Test, ob die in Phase 16 gewählte Architektur B (direkter MQTT-Zugriff des Browsers) für den Lesepfad wie gedacht funktioniert, bevor Phase 18 Schreibzugriffe hinzufügt.

## Voraussetzungen

- Phase 16 (Fundament & Architekturentscheidung) ✅
- WebSocket-Listener auf dem Mosquitto-Broker (`/etc/mosquitto/conf.d/websockets.conf`, Port 9001, `allow_anonymous true`) ✅ — vom Nutzer eingerichtet, siehe Umsetzung unten.

## Umsetzung (2026-08-17)

- **Broker-seitig** (vom Nutzer eingerichtet, Mosquitto 2.0.21 in einem Debian-LXC unter Proxmox): neue Datei `/etc/mosquitto/conf.d/websockets.conf` mit `listener 9001` / `protocol websockets` / `allow_anonymous true` (anonymer Zugriff, passend zum bestehenden 1883-Listener), `systemctl restart mosquitto`. Verifiziert per `ss -tlnp | grep 9001`.
- **Bibliotheken** (per `curl` von `unpkg.com` bezogen, lokal in `data/` abgelegt, kein CDN zur Laufzeit): `mqtt.min.js` (369 KB, MQTT.js-Browserbundle) und `alpine.min.js` (47 KB, Alpine.js 3.x CDN-Bundle). Beide zusammen ≈416 KB, passen komfortabel in die 1,875-MB-LittleFS-Partition.
- **`data/app.js`** (neu): Alpine.js-Komponente `dashboard()`, verbindet sich per `mqtt.connect("ws://192.168.1.123:9001/mqtt")` direkt mit dem Broker (Broker-Adresse fest hinterlegt, analog zu `tools/mqtt-tests/*.py` — liegt in einem anderen Netzsegment als die Geräte-IP, nicht automatisch ableitbar), abonniert `gartenwasser/#`. `handleMessage()` pflegt reaktiven State für Ventile (`V{n}/state`, `V{n}/auto/state`, `V{n}/alias`), Sequenz (`main/state`, `main/activeValve`, `main/remainingTotal`), aktives Programm (`main/program/state`) und Diagnostics (`diagnostics/i2cStatus`, `diagnostics/lastError`). `valveState()` repliziert exakt die Farblogik der Touch-UI-Ventilmatrix (`HmiManager::refreshValveStatus()`): rot = state AN (überschreibt), grün = auto AN + state AUS, dunkelgrau = auto AUS + state AUS, `V0` nie gedimmt.
- **`data/index.html`** (überarbeitet): Alpine-Template mit Kopfzeile (Verbindungsstatus Browser↔Broker + Geräte-Online-Status via `availability`), 2×3-Ventilkachel-Grid (`valve-tile`, Dashboard-Cards-Stil aus Phase 16), Sequenz-/Programm-Status-Karte, bedingt eingeblendete Fehler-Karte bei I2C-Problemen.
- **`data/style.css`**: `.header-bar`, `.valve-grid`/`.valve-tile`/`.valve-alias` ergänzt (Dashboard-Cards-Kacheln, farbcodiert wie oben).
- Keine Firmware-Änderung nötig — `WebManager` liefert die neuen Dateien unverändert über den bestehenden `serveStatic()`-Mechanismus aus.

## Betroffene Dateien

- `data/app.js` (neu)
- `data/index.html` (überarbeitet)
- `data/style.css` (ergänzt)
- `data/mqtt.min.js`, `data/alpine.min.js` (neu, Fremdbibliotheken)

## Test / Ergebnis

1. **Pipeline-Check ohne Browser**: `paho-mqtt` mit `transport="websockets"` gegen `192.168.1.123:9001/mqtt` verbunden (simuliert exakt, was `mqtt.js` im Browser tut) — Verbindung erfolgreich, 26 retained Nachrichten sofort empfangen. ✅
2. **Dateiauslieferung**: alle sechs Dateien (`/`, `/index.html`, `/style.css`, `/app.js`, `/mqtt.min.js`, `/alpine.min.js`) per `curl` mit korrekter Größe/HTTP 200 geprüft. ✅
3. **Im Browser** (Nutzer, gleiches Netz wie Broker): Seite zeigt „Verbunden“/„Gerät online“, Ventilkacheln korrekt farbig, Live-Update beim Schalten eines Ventils bestätigt. ✅ („ja, alles wie geplant!!“)
