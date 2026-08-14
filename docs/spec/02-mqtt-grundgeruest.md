# Phase 2 — MQTT-Grundgerüst

**Status:** 📋 Geplant

## Ziel

MQTT-Verbindung mit Last-Will-Testament (`availability`) als Basis für alle folgenden Phasen.

## Voraussetzungen

- Phase 1 (WLAN) ✅

## Umsetzung

- Neue Klasse `MqttManager` (`src/MqttManager.h/.cpp`), analog zu `WifiManager` aufgebaut:
  - `begin()`: PubSubClient mit Broker/Port/ClientID aus `secrets.h` konfigurieren, LWT auf `gartenwasser/availability` = `offline` (retained) setzen.
  - `loop()`: `client.loop()` aufrufen, bei Verbindungsverlust nicht-blockierend neu verbinden (analog zum WLAN-Reconnect-Muster aus Phase 1 — Intervall statt Busy-Loop).
  - Nach erfolgreichem Connect: `gartenwasser/availability` = `online` (retained) publizieren.
  - Nutzt `Logger` (Source `MQTT`) für alle Statusmeldungen.
- Spätere Automatik-Logik hängt **nicht** von einer bestehenden MQTT-Verbindung ab — `MqttManager` liefert nur Konnektivität, keine Ablaufsteuerung (siehe `docs/requirements.md`).
- NTP-Sync ergänzen (nach erfolgreichem WLAN-Connect), damit `Logger`/`diagnostics/lastError` (Phase 8) echte Zeitstempel statt boot-relativer Zeit verwenden.

## Betroffene Dateien

- `src/MqttManager.h`, `src/MqttManager.cpp` (neu)
- `src/main.cpp` (Einbindung in `setup()`/`loop()`)

## MQTT-Topics

- `gartenwasser/availability` (online/offline, LWT)

## Test

1. Mit MQTT Explorer/`mosquitto_sub -t gartenwasser/# -v` verbinden.
2. `availability` beobachten: `online` nach Boot.
3. Board vom Strom trennen → nach LWT-Timeout erscheint `offline`.
4. Board wieder verbinden → `online` erscheint erneut.
