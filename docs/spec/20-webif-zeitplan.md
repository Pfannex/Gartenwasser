# Phase 20 — Web-Interface: Zeitplan verwalten

**Status:** 📋 Geplant

## Ziel

Editor für Zeitplan-Einträge (Phase 15, `main/schedule/*`) — komplexestes Datenmodell des Web-Interfaces (typabhängige Felder `daily`/`weekly`/`once`, Programm-Referenz per Name). Zugleich der eigentliche fachliche Auslöser für das gesamte Web-Interface-Vorhaben: die Touch-UI-Zeitplanbedienung wurde verworfen, weil das 172×320px-Display dafür zu klein ist (siehe `docs/spec/13-touch-ui.md`).

## Voraussetzungen

- Phase 19 (Programme verwalten) ✅ — Programm-Auswahl für die `program`-Referenz wird hier benötigt

## Umsetzung

Details folgen nach Phase 16–19.

## Betroffene Dateien (voraussichtlich)

- `src/WebManager.h/.cpp` (falls Architektur A)
- `data/` (Zeitplan-Liste + typabhängiger Editor)

## Test (geplant)

1. Je einen `daily`/`weekly`/`once`-Eintrag über das Web-Interface anlegen → `main/schedule/state` korrekt, inkl. typspezifischer Felder.
2. Globalen Schalter (`enabled`) über die Weboberfläche umschalten.
3. Ungültige Kombinationen (z. B. `weekly` ohne Wochentag) werden im Formular verhindert oder zumindest beim Absenden klar zurückgemeldet.
4. Echter Trigger-Test wie bereits mehrfach durchgeführt (siehe `docs/testing.md`), diesmal mit einem über das Web-Interface angelegten Eintrag.
