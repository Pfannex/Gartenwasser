# Phase 17 — Web-Interface: Status-Dashboard (read-only)

**Status:** 📋 Geplant

## Ziel

Live-Übersicht im Browser: Ventilstatus (`V0`–`V5`), Automatik-Flags, laufende Sequenz samt Restlaufzeit, Diagnostics (`i2cStatus`/`lastError`) — inhaltliches Pendant zur Touch-UI-Hauptseite (Phase 13), aber am PC/Handy statt am Gerätedisplay. Bewusst **read-only**: erster echter Test, ob die in Phase 16 gewählte Architektur (A: eigene API / B: direkter MQTT-Zugriff des Browsers) für den Lesepfad wie gedacht funktioniert, bevor Phase 18 Schreibzugriffe hinzufügt.

## Voraussetzungen

- Phase 16 (Fundament & Architekturentscheidung) ✅

## Umsetzung

Abhängig von der in Phase 16 getroffenen Architekturentscheidung — Details folgen nach deren Klärung.

## Betroffene Dateien (voraussichtlich)

- `src/WebManager.h/.cpp` (falls Architektur A)
- `data/` (Dashboard-Seite, Dashboard-Cards-Stil aus Phase 16)

## Test (geplant)

1. Ventil manuell schalten (MQTT oder Touch) → Statusanzeige im Browser aktualisiert sich.
2. Automatik-Sequenz starten → laufendes Ventil + Restlaufzeit werden live angezeigt.
3. I2C-Fehler simulieren → Diagnostics-Anzeige im Browser reagiert.
