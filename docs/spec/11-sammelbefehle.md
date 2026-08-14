# Phase 11 — Sammel-Befehle (`main/time/set`, `main/auto/set`, JSON)

**Status:** 📋 Geplant

## Ziel

Mehrere Ventile in einem MQTT-Befehl konfigurieren (z. B. aus einer HA-Automatisierung heraus).

## Voraussetzungen

- Phase 5 (Laufzeit) ✅
- Phase 6 (Automatik-Flag) ✅

## Umsetzung

- `main/time/set`: JSON wie `{"maxTime":30,"V1":10,"V2":5}` parsen (via `ArduinoJson`). Der optionale Key `maxTime` setzt `main/time/maxTime` (siehe Phase 5), alle übrigen Keys (`V1`..`V5`) werden auf die einzelnen `V{n}/time/set`-Handler verteilt.
- `main/auto/set`: analog für `auto/set` (ohne `maxTime`-Sonderfall).
- Da rohes JSON nicht direkt in ein einzelnes HA-Discovery-Entity passt, ist das Topic vorerst nur für direkte MQTT-Nutzung/Automatisierungen gedacht (kein eigenes HA-Entity in Phase 10 vorgesehen).
- Payload-Validierung: unbekannte Ventil-Keys ignorieren, ungültige Werte wie in Phase 5 behandeln (ignorieren + loggen statt übernehmen).

## Betroffene Dateien

- `src/MqttManager.h/.cpp` (Subscribe-Handler für `main/time/set`, `main/auto/set`)

## MQTT-Topics

- `gartenwasser/main/time/set` (subscribe, JSON)
- `gartenwasser/main/auto/set` (subscribe, JSON)

## Test

1. `mosquitto_pub -t gartenwasser/main/time/set -m '{"maxTime":30,"V1":8,"V2":12}'` → `main/time/maxTime`, `V1/time/state` und `V2/time/state` werden aktualisiert.
2. `mosquitto_pub -t gartenwasser/main/auto/set -m '{"V1":true,"V3":false}'` → `V1/auto/state` und `V3/auto/state` werden aktualisiert.
3. Unbekannten Key (`"V9":5`) senden → wird ignoriert, kein Fehler/Crash.
