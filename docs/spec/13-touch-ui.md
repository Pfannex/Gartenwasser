# Phase 13 — Touch-UI (Automatik-Toggle & Statusanzeige)

**Status:** 📋 Geplant

## Ziel

Lokale Bedienung/Anzeige direkt am Gerät, ergänzend zur MQTT/Home-Assistant-Steuerung.

## Voraussetzungen

- Phase 4 (Ventile per MQTT) ✅ — Statusanzeige braucht die Ventil-States
- Phase 7 (Automatik-Sequenz) ✅ — Toggle-Button braucht `main/cmd`/`main/state`

## Umsetzung

- Erweiterung von `HmiManager` (`src/HmiManager.h/.cpp`), löst den aktuellen Platzhalter-Screen ab:
  - Toggle-Button „AUTO“/„OFF“, gekoppelt an `main/cmd`/`main/state` (Touch löst denselben Pfad aus wie ein MQTT-`cmd`, keine Sonderlogik).
  - Statusanzeige der Ventile `V0`–`V5` (z. B. Ist-Zustand ON/OFF, bei aktiver Automatik das laufende Ventil hervorgehoben — analog `main/activeValve`).
- `HmiManager` braucht dafür lesenden Zugriff auf den aktuellen Ventil-/Automatik-Zustand (z. B. über `ValveController`/`Sequencer`, keinen MQTT-Umweg für die lokale Anzeige).

## Betroffene Dateien

- `src/HmiManager.h`, `src/HmiManager.cpp`

## Test

1. Touch auf „AUTO“ → Automatik-Sequenz startet, identisch zu `main/cmd ON` per MQTT (auch `main/state`/`activeValve` in HA aktualisieren sich).
2. Touch auf „OFF“ während laufender Automatik → Sequenz stoppt sofort, wie bei `main/cmd OFF` per MQTT.
3. Ventil manuell per MQTT schalten → Statusanzeige auf dem Display aktualisiert sich.
4. Während Automatik-Lauf: aktives Ventil wird auf dem Display sichtbar hervorgehoben.
