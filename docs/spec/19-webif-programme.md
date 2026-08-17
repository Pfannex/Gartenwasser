# Phase 19 — Web-Interface: Programme verwalten

**Status:** 📋 Geplant

## Ziel

Volle Editier-Oberfläche für Bewässerungsprogramme (bis zu 32, Phase 14, `main/programs/*`) — Liste + Formular, erste wirklich komplexe UI des Web-Interfaces mit Hinzufügen/Entfernen/Umsortieren statt nur Werte ändern (Unterschied zu Phase 18).

## Voraussetzungen

- Phase 18 (Konfiguration bearbeiten) ✅ — Schreibpfad bereits verifiziert, hier zusätzlich Listen-/Array-Semantik

## Umsetzung

Details folgen nach Phase 16–18.

## Betroffene Dateien (voraussichtlich)

- `src/WebManager.h/.cpp` (falls Architektur A)
- `data/` (Programme-Liste + Editor)

## Test (geplant)

1. Neues Programm anlegen, Ventile mit `time`/`auto` belegen → `main/programs/state` korrekt, Touch-UI-Programme-Unterseite zeigt es beim nächsten Öffnen.
2. Programm löschen/umbenennen → Zeitplan-Referenzen (Phase 20, per Name) bleiben nachvollziehbar (kein automatisches Nachziehen, siehe `docs/spec/15-wochenplan.md`).
3. 32 Programme anlegen (bisher nur bis 5 getestet, siehe `docs/testing.md`) → erster echter Test der vollen Kapazität.
