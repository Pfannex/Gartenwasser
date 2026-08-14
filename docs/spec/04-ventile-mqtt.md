# Phase 4 — Ventile per MQTT (ohne Timer/Automatik)

**Status:** ✅ Erledigt & getestet

## Ziel

`V1`–`V5` per MQTT schaltbar machen, inkl. V0-Kopplung.

## Voraussetzungen

- Phase 2 (MQTT-Grundgerüst) ✅
- Phase 3 (ValveController) ✅

## Umsetzung

- `MqttManager` abonniert `gartenwasser/V{1..5}/cmd`.
- Bei `ON`: `Vn` **und** `V0` einschalten (Kopplung laut Funktionsbeschreibung).
- Bei `OFF`: `Vn` aus; `V0` nur aus, wenn kein anderes Ventil mehr aktiv ist.
- `state` je Ventil (inkl. `V0`) wird retained publiziert, sobald sich der Ist-Zustand ändert.
- Mehrere gleichzeitig geöffnete Ventile sind erlaubt.
- Manuelles `cmd ON` wird ignoriert, während eine Automatik-Sequenz läuft (siehe Phase 7). Manuelles `cmd OFF` wird **immer** angenommen — auch für das gerade aktive Ventil einer laufenden Automatik (die Sequenz macht dann mit dem nächsten Ventil weiter, siehe Phase 7).

## Betroffene Dateien

- `src/MqttManager.h/.cpp` (Subscribe-Handler)
- `src/ValveController.h/.cpp` (ggf. Erweiterung um „andere Ventile aktiv?“-Abfrage für V0-Logik)

## MQTT-Topics

- `gartenwasser/V{1..5}/cmd` (subscribe)
- `gartenwasser/V{0..5}/state` (publish, retained)

## Test

1. `mosquitto_pub -t gartenwasser/V1/cmd -m ON` → `V1/state` und `V0/state` werden `ON`, Relais schaltet hörbar/sichtbar.
2. Zweites Ventil `V2/cmd ON` → `V0` bleibt `ON`.
3. `V1/cmd OFF` → `V1/state OFF`, `V0` bleibt `ON` (da `V2` noch aktiv).
4. `V2/cmd OFF` → `V0/state OFF`.

## Test / Ergebnis

- Alle Ventile V1–V5 per MQTT (`cmd ON`/`OFF`) einzeln und in Kombination geschaltet, V0-Kopplung verifiziert (schaltet mit ein, bleibt an solange mind. ein Ventil aktiv, schaltet erst aus wenn alle aus sind) — funktioniert wie spezifiziert.
