# Phase 3 — ValveController (Hardware, ohne MQTT)

**Status:** ✅ Erledigt & getestet

## Ziel

Ventilsteuerung als eigene Klasse kapseln, unabhängig von MQTT testbar.

## Voraussetzungen

- Phase 0 (I2CManager) ✅

## Umsetzung

- Neue Klasse `ValveController` (`src/ValveController.h/.cpp`):
  - `setValve(uint8_t index, bool on)`, `getValve(uint8_t index)` für `V0`–`V5`.
  - Nutzt `I2CManager::writeRegister()` mit der Pinbelegung aus `docs/requirements.md` (V0=B7, V1=B2, V2=B3, V3=B4, V4=B5, V5=B6).
  - **Boot-Grundzustand**: alle Ventile AUS (siehe `docs/requirements.md`, Abschnitt „Code Anforderungen“).
  - Nutzt `Logger` (Source `I2C`, da Hardware-nah) für Fehlermeldungen.
- Zum Testen temporär per Serial-Kommando schaltbar (z. B. `v1on`/`v1off`), wird nach Phase 4 (MQTT-Anbindung) wieder entfernt.

## Betroffene Dateien

- `src/ValveController.h`, `src/ValveController.cpp` (neu)

## Test

1. Firmware mit temporärem Serial-Test-Handler flashen.
2. Jedes Ventil/Relais (`V0`–`V5`) einzeln per Serial-Befehl schalten.
3. Hardware-Reaktion (Relais/LED) prüfen.
4. `getValve()`-Rücklesung gegen tatsächlichen Schaltzustand verifizieren.

## Test / Ergebnis

- Geflasht per PlatformIO „Upload and Monitor“, alle Ventile V0–V5 per Serial-Testbefehle (`v0on`…`v5on`/`v0off`…`v5off`) einzeln geschaltet — Hardware-Ausgänge laufen einwandfrei.
