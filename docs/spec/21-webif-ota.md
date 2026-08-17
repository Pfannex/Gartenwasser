# Phase 21 — Web-Interface: Firmware-Update (OTA)

**Status:** 📋 Geplant

## Ziel

Firmware-Updates über die Weboberfläche statt per USB/`esptool`. Bewusst als letzte Web-Interface-Phase eingeordnet — höchstes Risiko (Brick-Gefahr bei fehlgeschlagenem Update), unabhängig vom Rest des Web-Interfaces, soll erst kommen, wenn Phasen 16–20 stabil laufen.

## Voraussetzungen

- Phase 16 (liefert `ESPAsyncWebServer`) ✅
- Partitionstabelle (`partitions.csv`) hat bereits eine vollständige Dual-OTA-Auslegung (`app0`/`app1`, je 3 MB, `otadata`) — keine Änderung nötig, siehe Ressourcen-Check in `docs/spec/16-webif-fundament.md`.

## Umsetzung (geplant)

- `ElegantOTA` (Library) an `ESPAsyncWebServer` andocken — fertige Upload-Seite + Endpoint (Fortschrittsanzeige, Verifizierung, Reboot), statt `Update.h` von Hand zu bedienen.
- Flash-Größen-Check vor Beginn dieser Phase (kumulativ aus den Checkpoints der Phasen 16–20) — Ziel: deutliche Marge zum 3-MB-Limit eines einzelnen `app`-Slots.

## Betroffene Dateien (voraussichtlich)

- `platformio.ini` (neue `lib_deps`: `ElegantOTA`)
- `src/WebManager.h/.cpp`

## Test (geplant)

1. Firmware-Binary über die Weboberfläche hochladen → Gerät verifiziert, rebootet, läuft mit neuer Version.
2. Absichtlich fehlerhafte/unvollständige Datei hochladen → wird abgelehnt, altes Firmware-Image bleibt unangetastet und bootfähig.
3. Nach erfolgreichem Update: Persistenz (`config`/`programs`/`schedule`) unverändert erhalten (SPIFFS/LittleFS-Partition ist von der App-OTA unabhängig).
