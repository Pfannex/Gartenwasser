# Phase 9 — Alias je Ventil

**Status:** 📋 Geplant

## Ziel

Editierbarer Klartextname je Ventil, persistent über Neustart hinweg.

## Voraussetzungen

- Phase 4 (Ventile per MQTT) ✅
- Phase 5 (`ConfigStore` existiert bereits) ✅

## Umsetzung

- `alias/set` → `alias` (retained Text) je Ventil.
- Persistenz via `ConfigStore` (SPIFFS, siehe Phase 5).
- Sinnvolle Länge/Zeichen-Validierung für den Alias-Text (Payload-Größe begrenzen).

## Betroffene Dateien

- `src/ValveController.h/.cpp`
- `src/ConfigStore.h/.cpp` (Erweiterung um `alias`-Werte)

## MQTT-Topics

- `gartenwasser/V{1..5}/alias/set` (subscribe)
- `gartenwasser/V{1..5}/alias` (publish, retained)

## Test

1. `mosquitto_pub -t gartenwasser/V2/alias/set -m "Rasen Seite"` → `alias`-Topic wird retained aktualisiert.
2. Neustart des Boards → Alias bleibt erhalten (Persistenz-Test).
