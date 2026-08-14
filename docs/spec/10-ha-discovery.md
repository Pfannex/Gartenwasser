# Phase 10 — Home Assistant MQTT-Discovery

**Status:** 📋 Geplant

## Ziel

Automatische Geräte-/Entity-Erkennung in Home Assistant ohne manuelle YAML-Konfiguration.

## Voraussetzungen

- Phasen 2–9 (alle MQTT-Topics existieren bereits) ✅

## Umsetzung

- Neue Klasse `HaDiscovery` (`src/HaDiscovery.h/.cpp`):
  - Baut für jede Entity (siehe Mapping-Tabelle in `docs/requirements.md`) eine Discovery-Config (JSON, via `ArduinoJson`) und publiziert sie retained unter `homeassistant/<component>/gartenwasser/<object_id>/config`.
  - Gemeinsames `device`-Objekt (`identifiers: ["gartenwasser"]`) und `availability`-Referenz auf `gartenwasser/availability` in jeder Config.
  - Wird nach jedem erfolgreichen `MqttManager`-Connect erneut publiziert (Discovery-Configs sind retained, aber ein Re-Publish nach Reconnect stellt sicher, dass HA nach eigenem Neustart die Entities wiederfindet).

## Betroffene Dateien

- `src/HaDiscovery.h`, `src/HaDiscovery.cpp` (neu)

## Test

1. Home Assistant mit MQTT-Integration verbinden lassen (oder Board neu starten).
2. Gerät „Gartenbewässerung“ erscheint automatisch unter Einstellungen → Geräte, mit allen Entities aus der Mapping-Tabelle.
3. Schalter/Number-Entities aus der HA-UI bedienen → Hardware-Reaktion prüfen.
4. Board stromlos schalten → Entities zeigen in HA „nicht verfügbar“ (Availability/LWT-Test).
