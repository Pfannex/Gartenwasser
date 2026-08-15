# Phase 7 — Automatik-Sequenz

**Status:** ✅ Erledigt & getestet

## Ziel

Übergeordnete Start/Stop-Funktion, die die Ventile mit `auto=ON` nacheinander ansteuert.

## Voraussetzungen

- Phase 5 (Laufzeit & Restlaufzeit, `maxTime`) ✅
- Phase 6 (Automatik-Flag) ✅

## Umsetzung

- Neue Klasse `Sequencer` (`src/Sequencer.h/.cpp`):
  - `main/cmd ON` startet Ablauf über alle Ventile mit `auto=ON` in Reihenfolge `V1`→`V5`, je `min(time[n], maxTime)` Minuten (siehe Phase 5).
  - `main/activeValve` zeigt das aktuelle Ventil (`"V1".."V5"` oder `"-"`).
  - `main/remainingTotal` zeigt die Restzeit der Gesamtsequenz (Summe der verbleibenden Zeiten der noch offenen Ventile — durch `maxTime` implizit auf maximal `5 × maxTime` begrenzt).
  - `main/cmd OFF` (jederzeit): alle Ventile aus, alle Timer auf konfiguriertes `time` zurücksetzen, `main/state OFF`, `activeValve "-"`.
  - Nach natürlichem Ablaufende: gleicher Reset.
  - Läuft **lokal/autonom** auf dem ESP32 weiter, unabhängig von WLAN/MQTT-Konnektivität.
  - **Manuelles `V{n}/cmd ON`** wird während der Automatik ignoriert.
  - **Manuelles `V{n}/cmd OFF`** des gerade aktiven Ventils wird angenommen: Der `Sequencer` behandelt das identisch zu einem regulären Zeitablauf (Timer erreicht `00:00`) und fährt sofort mit dem nächsten Ventil (`auto=ON`) fort — **kein** Abbruch der Gesamtsequenz.
  - Erreichen von `maxTime` für das aktive Ventil wird technisch identisch behandelt (siehe Phase 5) — auch hier macht die Sequenz einfach mit dem nächsten Ventil weiter.

## Betroffene Dateien

- `src/Sequencer.h`, `src/Sequencer.cpp` (neu)

## MQTT-Topics

- `gartenwasser/main/cmd` (subscribe)
- `gartenwasser/main/state` (publish)
- `gartenwasser/main/activeValve` (publish)
- `gartenwasser/main/remainingTotal` (publish, sekündlich)

## Test

1. `V1` und `V3` auf `auto=ON` setzen, andere auf `OFF`, kurze Zeiten (30 s).
2. `main/cmd ON` senden → Sequenzverlauf beobachten: `activeValve` wechselt `V1` → `V3` → `-`.
3. Relais-Verhalten während des Durchlaufs prüfen (nur jeweils aktives Ventil + `V0` an).
4. Während des Laufs `main/cmd OFF` senden → sofortiger Abbruch, alle Ventile aus, Timer zurückgesetzt.
5. Während des Laufs (V1 aktiv) `V1/cmd OFF` per MQTT senden → Sequenz fährt sofort mit `V3` fort (`activeValve` wechselt vorzeitig, kein Abbruch).
6. Während des Laufs `V3/cmd ON` senden (V1 ist aktiv) → wird ignoriert, `activeValve` bleibt `V1`.
7. `maxTime` kleiner als `time` eines Ventils setzen → Sequenz wechselt beim Erreichen von `maxTime` normal zum nächsten Ventil.
8. Nach natürlichem Ablaufende: alle Ventile aus, alle Timer auf `time` zurückgesetzt, `activeValve "-"`.

## Test / Ergebnis

- Sequenzverlauf, vorzeitiges Weiterschalten per manuellem `V{n}/cmd OFF` und Abbruch per `main/cmd OFF` auf Hardware getestet — funktioniert wie spezifiziert.
- Nachtrag (siehe `docs/Log.md`, 2026-08-15): Restlaufzeit-Anzeige verfeinert — Ventile, die noch nicht an der Reihe waren, zeigen `remaining = time` (armiert); bereits durchgelaufene Ventile zeigen `remaining = 00:00`, solange die Sequenz noch läuft; erst nach Sequenz-Ende (natürlich oder `main/cmd OFF`) werden alle Restlaufzeiten wieder auf `time` gesetzt.
