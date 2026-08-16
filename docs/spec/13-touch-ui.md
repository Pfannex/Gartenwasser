# Phase 13 — Touch-UI (Automatik-Toggle & Statusanzeige)

**Status:** ✅ Erledigt & getestet

## Ziel

Lokale Bedienung/Anzeige direkt am Gerät, ergänzend zur MQTT/Home-Assistant-Steuerung.

## Voraussetzungen

- Phase 4 (Ventile per MQTT) ✅ — Statusanzeige braucht die Ventil-States
- Phase 7 (Automatik-Sequenz) ✅ — Toggle-Button braucht `main/cmd`/`main/state`

## Umsetzung

- Erweiterung von `HmiManager` (`src/HmiManager.h/.cpp`), löst den ursprünglichen Platzhalter-Screen ab:
  - Toggle-Button „AUTO“/„OFF“, gekoppelt an `main/cmd`/`main/state` (Touch löst denselben Pfad aus wie ein MQTT-`cmd`, keine Sonderlogik).
  - Ventile `V0`–`V5` als runde Status-Indikatoren (`lv_led`, radio-button-artig): grün = AUS, rot = AN. Ventile mit `auto=OFF` werden im AUS-Zustand dunkelgrau gedimmt (nicht Teil der Automatik-Sequenz), springen bei manueller Einschaltung aber normal auf Rot. Aktives Sequenz-Ventil wird zusätzlich gelb hervorgehoben.
  - Statuszeile als Fußleiste unten (dunkelgrauer Hintergrund, volle Breite/verbleibende Höhe): zeigt priorisiert an, was gerade passiert — I2C-Fehler > laufende Automatik (Alias/Ventilname + Restlaufzeit) > manuell laufende Ventile (+ Anzahl weiterer) > gewähltes Programm (Platzhalter) > „Bereit“.
  - 4 Platzhalter-Toggle-Buttons „P1“–„P4“ rechts (Radio-Verhalten, nur einer gleichzeitig aktiv) — für Phase 14 (Bewässerungsprogramme), aktuell ohne Funktion außer einem Log-Eintrag.
- `HmiManager` liest den Zustand direkt aus `ValveController`/`Sequencer`/`Diagnostics`/`ValveTimer` (kein MQTT-Umweg für die lokale Anzeige).
- `MqttManager::requestMainCmd(bool)` (neu, öffentlich) kapselt den Aufruf von `startSequence()`/`stopSequence()` — von MQTT-`main/cmd` und Touch gleichermaßen genutzt, keine doppelte Logik.
- Alias-Texte mit Umlauten werden für die Display-Anzeige lokal auf ASCII transliteriert (ä→ae, ö→oe, ü→ue, ß→ss) — die eingebauten LVGL-Fonts enthalten keine Umlaute. Die eigentlichen Alias-Werte (MQTT/`ConfigStore`) bleiben unverändert UTF-8.

## Betroffene Dateien

- `src/HmiManager.h`, `src/HmiManager.cpp`
- `src/MqttManager.h`, `src/MqttManager.cpp` (neue `requestMainCmd()`, wie in vorherigen Phasen nicht in der ursprünglichen Liste, aber notwendig)

## Test

1. Touch auf „AUTO“ → Automatik-Sequenz startet, identisch zu `main/cmd ON` per MQTT (auch `main/state`/`activeValve` in HA aktualisieren sich).
2. Touch auf „OFF“ während laufender Automatik → Sequenz stoppt sofort, wie bei `main/cmd OFF` per MQTT.
3. Ventil manuell per MQTT schalten → Statusanzeige auf dem Display aktualisiert sich.
4. Während Automatik-Lauf: aktives Ventil wird auf dem Display sichtbar hervorgehoben.

## Test / Ergebnis

- Alle vier Testfälle auf Hardware verifiziert, vom Nutzer bestätigt ("sieht klasse aus").
- Iterativ verfeinert: LED-Statusindikatoren statt Text-ON/OFF, Dimmen bei `auto=OFF`, Umlaut-Transliteration, Statuszeile als abgesetzte Fußleiste, P1–P4-Platzhalter-Buttons mit Radio-Verhalten.
