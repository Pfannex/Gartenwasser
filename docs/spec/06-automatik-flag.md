# Phase 6 — Automatik-Flag je Ventil

**Status:** ✅ Erledigt & getestet

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

## Test / Ergebnis

- `auto/set` getestet, `auto/state` retained bestätigt, Wert übersteht Neustart (Persistenz via `ConfigStore`/SPIFFS).

## Abweichung von der Spec (während der Umsetzung)

- Wie in den vorherigen Phasen: die MQTT-Anbindung (`auto/set`-Handler, State-Publish) liegt in `MqttManager.cpp`, nicht nur in `ValveController`/`ConfigStore`.
- `ValveController::getAuto()`/`setAuto()` sind dünne Facaden über `ConfigStore` — passend zur Rolle von `ValveController` als Anlaufstelle für alle Ventil-Eigenschaften (analog zu Phase 9, `alias`).
