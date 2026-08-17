# Phase 18 — Web-Interface: Konfiguration bearbeiten

**Status:** 📋 Geplant

## Ziel

`time`/`auto`/`alias`/`maxTime` je Ventil (Datenmodell aus Phase 11, `main/config/*`) im Browser editierbar machen — einfachstes Datenmodell des Web-Interfaces, damit der Schreibpfad (im Gegensatz zum reinen Lesen aus Phase 17) zuerst am simpelsten Fall erprobt wird, bevor Phase 19/20 komplexere Datenmodelle (Listen, Teilmengen-Semantik, typabhängige Felder) hinzufügen.

## Voraussetzungen

- Phase 17 (Status-Dashboard) ✅ — Lesepfad der gewählten Architektur bereits verifiziert

## Umsetzung

Details folgen nach Phase 16/17.

## Betroffene Dateien (voraussichtlich)

- `src/WebManager.h/.cpp` (falls Architektur A)
- `data/` (Konfigurationsformular)

## Test (geplant)

1. Laufzeit/Alias/Automatik-Flag über das Web-Interface ändern → `V{n}/*` state-Topics und Persistenz (`config.json`) korrekt.
2. Ungültige Werte (zu lange Alias, negative Laufzeit) werden abgelehnt, wie bei den bestehenden MQTT-Validierungen.
