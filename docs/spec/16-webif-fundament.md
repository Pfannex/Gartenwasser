# Phase 16 — Web-Interface: Fundament & Architekturentscheidung

**Status:** 📋 Geplant (Architektur noch offen, wird im Interview mit dem Nutzer geklärt)

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

## Noch offene Fragen (im Interview zu klären, vor Umsetzungsbeginn)

1. **Architektur A vs. B** — die zentrale, alles andere prägende Entscheidung:
   - **A) ESP32 hostet eine eigene REST/WebSocket-API.** Web-Interface spricht nur mit dem Gerät. Mehr Code auf dem ESP32 (neue Endpoints, JSON-Bridging zu `ConfigStore`/`MqttManager`), funktioniert aber unabhängig vom Broker.
   - **B) ESP32 liefert nur statische Dateien aus, der Browser verbindet sich per MQTT-over-WebSocket direkt mit dem Broker** (z. B. via `mqtt.js`), nutzt exakt dieselben Topics wie mqtt-spy/Home Assistant. Kaum neue Logik auf dem Gerät, volle Wiederverwendung des bestehenden MQTT-Modells — braucht aber einen WebSocket-Listener auf dem Mosquitto-Broker (externe Config-Änderung, nicht Teil der Firmware).
   - Tendenz aus der Diskussion: B passt zur bereits stark MQTT-zentrierten Architektur des Projekts — noch nicht final entschieden.
2. **Frontend-Ansatz**: Vanilla HTML/CSS/JS, ein leichtgewichtiges Reaktivitäts-Framework (Alpine.js/htmx) oder ein volles SPA-Framework mit Build-Schritt (Vue/Svelte/Preact). Der gewählte „Dashboard Cards“-Stil braucht für Listen (Programme/Zeitplan) spürbar mehr Interaktivität als „Native Minimal“ — tendenziell Alpine.js/htmx-Niveau, noch zu bestätigen.
3. **Dateisystem**: `SPIFFS` → `LittleFS` wechseln (moderner Nachfolger, bessere Eignung für mehrere benannte Dateien wie `index.html`/`style.css`/`app.js`) — Empfehlung steht, betrifft `ConfigStore`s Dateizugriff (Persistenz-Regressionsrisiko), noch zu bestätigen.

## Geplante Umsetzung (nach Klärung der offenen Fragen)

- `ESPAsyncWebServer` + `AsyncTCP` als neue Abhängigkeit (`platformio.ini`) — De-facto-Standard für nicht-blockierende ESP32-Webserver, passt zum durchgehaltenen Non-Blocking-Prinzip des Projekts (`WebServer.h` wäre synchron/blockierend, bewusst nicht gewählt).
- Ggf. Dateisystem-Wechsel `SPIFFS` → `LittleFS`.
- Minimale statische Seite ausliefern (kein Fachinhalt) — beweist die komplette Kette WLAN → Webserver → Dateisystem → Browser end-to-end, bevor Phase 17 Fachlogik aufsetzt.
- Dashboard-Cards-CSS-Basis (Farb-/Typografie-Tokens) als gemeinsame Grundlage ablegen.

## Betroffene Dateien (voraussichtlich)

- `platformio.ini` (neue `lib_deps`, ggf. `board_build.filesystem`)
- `src/WebManager.h/.cpp` (neu, analog zu `MqttManager`/`HmiManager` als eigenständige Klasse)
- `src/main.cpp` (Einbindung `WebManager::begin()`/`loop()`)
- `data/` (neues PlatformIO-Verzeichnis für LittleFS-Inhalte, falls Option „statische Dateien“ gewählt wird)

## Test (geplant)

1. Seite lädt im Browser über die Geräte-IP.
2. Flash-Größen-Check nach Abschluss der Phase (siehe Ressourcen-Checkpoint oben).
