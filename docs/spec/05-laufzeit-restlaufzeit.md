# Phase 5 — Laufzeit & Restlaufzeit je Ventil

**Status:** 📋 Geplant

## Ziel

Einstellbare Einschaltdauer je Ventil, mit sekündlich aktualisierter Restlaufzeit, automatischem Abschalten und einer harten `maxTime`-Obergrenze als Failsafe.

## Voraussetzungen

- Phase 4 (Ventile per MQTT) ✅

## Umsetzung

- Neue Klasse `ValveTimer` (`src/ValveTimer.h/.cpp`) oder Erweiterung von `ValveController`: pro Ventil `time` (Minuten) und `remaining` (Sekunden-Countdown).
- Neue Klasse `ConfigStore` (`src/ConfigStore.h/.cpp`): Persistenz aller Einstellwerte im SPIFFS (JSON-Datei, z. B. `/config.json`). Wird von `ValveTimer` (`time`, `maxTime`), Phase 6 (`auto`) und Phase 9 (`alias`) gemeinsam genutzt — hier eingeführt, da `time` der erste persistente Wert ist.
- `time/set` → validieren (effektiver Wert wird durch `maxTime` gedeckelt, siehe unten), dann `time/state` retained publizieren und in `ConfigStore` sichern.
- `main/time/maxTime`: harte Obergrenze **pro Ventil**. Effektive Laufzeit = `min(time, maxTime)`. Gilt für manuelles und automatisches Einschalten gleichermaßen. Wird ebenfalls über `ConfigStore` persistiert.
- Bei Einschaltung eines Ventils (manuell oder Automatik): Countdown von `min(time, maxTime)` starten.
- `time/remaining` sekündlich als `mm:ss` publizieren.
- Bei Erreichen von `00:00`: Ventil automatisch ausschalten (inkl. V0-Kopplungslogik aus Phase 4). Dieses Verhalten ist identisch, unabhängig davon, ob `time` oder `maxTime` den Countdown begrenzt hat — dadurch braucht die Automatik-Sequenz (Phase 7) keinen Sonderfall für „`maxTime` erreicht“.

## Betroffene Dateien

- `src/ValveTimer.h`, `src/ValveTimer.cpp` (neu, oder Erweiterung `ValveController`)
- `src/ConfigStore.h`, `src/ConfigStore.cpp` (neu)

## MQTT-Topics

- `gartenwasser/V{1..5}/time/set` (subscribe)
- `gartenwasser/V{1..5}/time/state` (publish, retained)
- `gartenwasser/V{1..5}/time/remaining` (publish, sekündlich)
- `gartenwasser/main/time/maxTime` (publish, retained; Wert wird über `main/time/set`-JSON gesetzt, siehe Phase 11)

## Test

1. `maxTime` auf 2 Minuten setzen, `time/set` für `V2` auf 5 Minuten.
2. `V2` einschalten → `time/remaining` startet bei `02:00` (nicht `05:00`), da `maxTime` greift.
3. Ventil schaltet bei `00:00` automatisch ab.
4. `time/set` innerhalb `maxTime` (z. B. 1 Minute) → Countdown startet regulär bei `01:00`.
5. Ungültigen Wert senden (`time/set -5` bzw. nicht-numerisch) → wird ignoriert, `time/state` unverändert, Log-Eintrag `ERROR`.
6. Neustart des Boards → `time`- und `maxTime`-Werte bleiben erhalten (Persistenz-Test via `ConfigStore`).
