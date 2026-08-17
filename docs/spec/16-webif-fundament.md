# Phase 16 — Web-Interface: Fundament & Architekturentscheidung

**Status:** 📋 Geplant (Design vollständig abgestimmt, Umsetzung steht an)

## Ziel

Technisches und architektonisches Fundament für das Web-Interface, bevor in den Folgephasen (17–21) Fachfunktionen darauf aufgebaut werden. Auslöser: die Touch-UI-Zeitplanbedienung wurde verworfen (Display zu klein, siehe `docs/spec/13-touch-ui.md`), komfortables Editieren von Programmen/Zeitplan soll stattdessen über eine Weboberfläche laufen. Bewusst **vor** Phase 10 (Home Assistant) eingeordnet.

## Voraussetzungen

- Alle geräteinternen Phasen (00–09, 11–15) ✅
- Touch-UI-Neugestaltung (Phase 13, Nachtrag) ✅ — liefert die visuelle Referenz (Statusfarben Grün/Rot/Dunkelgrau für Ventile) und bestätigt die Ressourcenlage (RAM/Flash-Check, siehe unten)

## Ressourcen-Check (2026-08-17, vorab durchgeführt)

- Flash: `app0`-Partition 3 MB, aktuell 41,5 % belegt (≈1,31 MB). Partitionstabelle (`partitions.csv`) hat bereits eine vollständige Dual-OTA-Auslegung (`app0`/`app1`, je 3 MB, `otadata`) — keine Änderung für Phase 21 nötig.
- `spiffs`-Partition: 1,875 MB, aktuell fast leer (nur die drei kleinen Config-JSONs) — ausreichend Platz für das komplette Web-Interface-Bundle.
- RAM: 320 KB, aktuell 34,6 % belegt (≈114 KB frei... genauer: ≈214 KB frei).
- Grobe Schätzung für den gesamten Web-Interface-Umfang (Phasen 16–21): ≈1,7 MB von 3 MB (≈56 %) — reichlich Marge. Größter einzelner Sprung erwartet direkt in dieser Phase (Webserver-Bibliotheken erstmals einkompiliert) — **Checkpoint: Flash-Größe nach Abschluss dieser Phase gegenprüfen.**

## Gewählte Design-Richtung

**Visueller Stil: „Dashboard Cards“** (2026-08-17 vom Nutzer gewählt, siehe Stilrichtungen-Vergleich) — farbige Statuschips, weiche Karten, nah an der Home-Assistant-Formsprache. CSS-Tokens (Farben/Typografie/Radius) aus diesem Stil bilden die gemeinsame Basis für die Phasen 17–20.

## Entschiedene Punkte

1. **Architektur: Option B** (2026-08-17 entschieden). ESP32 liefert nur statische Dateien aus (HTML/CSS/JS via `ESPAsyncWebServer`), der Browser verbindet sich per MQTT-over-WebSocket **direkt** mit dem Broker (z. B. via `mqtt.js`) und nutzt exakt dieselben Topics wie mqtt-spy/Home Assistant. Kaum neue Logik auf dem ESP32 (kein REST-/State-Bridging nötig), volle Wiederverwendung des bestehenden MQTT-Modells. Begründung: passt zur bereits stark MQTT-zentrierten Architektur des Projekts.
   - **Voraussetzung außerhalb der Firmware**: WebSocket-Listener auf dem Mosquitto-Broker (z. B. `listener 9001` + `protocol websockets` in der Broker-Config) — nicht Teil dieser Phase, muss separat eingerichtet werden.
   - **Netzwerk-Hinweis**: funktioniert direkt, wenn das Gerät (Handy/PC), von dem aus die Weboberfläche geöffnet wird, im selben Subnetz wie der Broker ist (Normalfall im Heimnetz). Für Zugriff von unterwegs (z. B. Mobilfunknetz statt Heim-WLAN) müsste der Broker zusätzlich über denselben Weg erreichbar sein wie der ESP32 selbst (siehe frühere VPN-Kopplung Heimnetz↔Ferienhaus, `docs/requirements.md`) — reine Netzwerk-/Infrastrukturfrage, kein Firmware-Thema, hier nur als Hinweis festgehalten.
2. **Frontend-Ansatz: Alpine.js** (2026-08-17 entschieden). Leichtgewichtig (~15–40 KB), kein Build-Schritt, direkt als einzelne Datei in LittleFS ablegbar. Deklarative Reaktivität für Listen (Programme/Zeitplan) und Live-Updates aus eintreffenden MQTT-Nachrichten, ohne den Aufwand eines vollen SPA-Frameworks mit Build-Pipeline.
3. **Dateisystem: `LittleFS`** statt `SPIFFS` (moderner Nachfolger, bessere Eignung für mehrere benannte Dateien wie `index.html`/`style.css`/`app.js`) — betrifft `ConfigStore`s Dateizugriff, bei der Umsetzung auf Persistenz-Regression prüfen (`config.json`/`programs.json`/`schedule.json` müssen nach dem Wechsel weiterhin korrekt geladen werden).

## Geplante Umsetzung

- `ESPAsyncWebServer` + `AsyncTCP` als neue Abhängigkeit (`platformio.ini`) — De-facto-Standard für nicht-blockierende ESP32-Webserver, passt zum durchgehaltenen Non-Blocking-Prinzip des Projekts (`WebServer.h` wäre synchron/blockierend, bewusst nicht gewählt). Dank Architekturentscheidung B beschränkt sich `WebManager` rein auf statisches File-Serving — keine REST-Endpoints, kein JSON-Bridging zu `ConfigStore`/`MqttManager` nötig, das übernimmt der Browser direkt per MQTT.
- Dateisystem-Wechsel `SPIFFS` → `LittleFS` (`board_build.filesystem` in `platformio.ini`, `ConfigStore` auf `LittleFS.h` statt `SPIFFS.h` umstellen).
- Minimale statische Seite ausliefern (kein Fachinhalt) — beweist die komplette Kette WLAN → Webserver → Dateisystem → Browser end-to-end, bevor Phase 17 den MQTT-over-WebSocket-Client (Alpine.js + `mqtt.js`) aufsetzt.
- Dashboard-Cards-CSS-Basis (Farb-/Typografie-Tokens) als gemeinsame Grundlage ablegen.

## Betroffene Dateien (voraussichtlich)

- `platformio.ini` (neue `lib_deps`, ggf. `board_build.filesystem`)
- `src/WebManager.h/.cpp` (neu, analog zu `MqttManager`/`HmiManager` als eigenständige Klasse — bleibt dank Architektur B schlank, reines File-Serving)
- `src/main.cpp` (Einbindung `WebManager::begin()`/`loop()`)
- `data/` (neues PlatformIO-Verzeichnis für LittleFS-Inhalte)

## Test (geplant)

1. Seite lädt im Browser über die Geräte-IP.
2. Flash-Größen-Check nach Abschluss der Phase (siehe Ressourcen-Checkpoint oben).
