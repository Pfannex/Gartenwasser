# Phase 6 — Automatik-Flag je Ventil

**Status:** 📋 Geplant

## Ziel

Pro Ventil ein persistentes Flag, das festlegt, ob es Teil der Automatik-Sequenz (Phase 7) ist.

## Voraussetzungen

- Phase 4 (Ventile per MQTT) ✅
- Phase 5 (`ConfigStore` existiert bereits) ✅

## Umsetzung

- `auto/set` → `auto/state` (retained bool) je Ventil.
- Zunächst nur Speicherung, keine Funktion (wird von der Sequenz in Phase 7 gelesen).
- Persistenz über Neustart hinweg via `ConfigStore` (SPIFFS, siehe Phase 5).

## Betroffene Dateien

- `src/ValveController.h/.cpp`
- `src/ConfigStore.h/.cpp` (Erweiterung um `auto`-Werte)

## MQTT-Topics

- `gartenwasser/V{1..5}/auto/set` (subscribe)
- `gartenwasser/V{1..5}/auto/state` (publish, retained)

## Test

1. `mosquitto_pub -t gartenwasser/V3/auto/set -m ON` → `auto/state` wird retained `ON`.
2. Neustart des Boards → `auto/state` bleibt `ON` (Persistenz-Test).
