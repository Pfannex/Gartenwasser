# Phase 16 — Web-Interface: Fundament & Architekturentscheidung

**Status:** ✅ Erledigt & getestet

## Ziel

Technisches und architektonisches Fundament für das Web-Interface, bevor in den Folgephasen (17–21) Fachfunktionen darauf aufgebaut werden. Auslöser: die Touch-UI-Zeitplanbedienung wurde verworfen (Display zu klein, siehe `docs/spec/13-touch-ui.md`), komfortables Editieren von Programmen/Zeitplan soll stattdessen über eine Weboberfläche laufen. Bewusst **vor** Phase 10 (Home Assistant) eingeordnet.

## Voraussetzungen

- Alle geräteinternen Phasen (00–09, 11–15) ✅
- Touch-UI-Neugestaltung (Phase 13, Nachtrag) ✅ — liefert die visuelle Referenz (Statusfarben Grün/Rot/Dunkelgrau für Ventile) und bestätigt die Ressourcenlage (RAM/Flash-Check, siehe unten)

## Ressourcen-Check (2026-08-17, vorab durchgeführt)

- Flash: `app0`-Partition 3 MB, aktuell 41,5 % belegt (≈1,31 MB). Partitionstabelle (`partitions.csv`) hat bereits eine vollständige Dual-OTA-Auslegung (`app0`/`app1`, je 3 MB, `otadata`) — keine Änderung für Phase 21 nötig.
- `spiffs`-Partition: 1,875 MB, aktuell fast leer (nur die drei kleinen Config-JSONs) — ausreichend Platz für das komplette Web-Interface-Bundle.
- RAM: 320 KB, aktuell 34,6 % belegt (≈114 KB frei... genauer: ≈214 KB frei).
- Grobe Schätzung für den gesamten Web-Interface-Umfang (Phasen 16–21): ≈1,7 MB von 3 MB (≈56 %) — reichlich Marge. Größter einzelner Sprung erwartet direkt in dieser Phase (Webserver-Bibliotheken erstmals einkompiliert) — **Checkpoint: Flash-Größe nach Abschluss dieser Phase gegenprüfen.**

## Gewählte Design-Richtung

**Visueller Stil: „Dashboard Cards“** (2026-08-17 vom Nutzer gewählt, siehe Stilrichtungen-Vergleich) — farbige Statuschips, weiche Karten, nah an der Home-Assistant-Formsprache. CSS-Tokens (Farben/Typografie/Radius) aus diesem Stil bilden die gemeinsame Basis für die Phasen 17–20.

## Entschiedene Punkte

1. **Architektur: Option B** (2026-08-17 entschieden). ESP32 liefert nur statische Dateien aus (HTML/CSS/JS via `ESPAsyncWebServer`), der Browser verbindet sich per MQTT-over-WebSocket **direkt** mit dem Broker (z. B. via `mqtt.js`) und nutzt exakt dieselben Topics wie mqtt-spy/Home Assistant. Kaum neue Logik auf dem ESP32 (kein REST-/State-Bridging nötig), volle Wiederverwendung des bestehenden MQTT-Modells. Begründung: passt zur bereits stark MQTT-zentrierten Architektur des Projekts.
   - **Voraussetzung außerhalb der Firmware**: WebSocket-Listener auf dem Mosquitto-Broker (z. B. `listener 9001` + `protocol websockets` in der Broker-Config) — nicht Teil dieser Phase, muss separat eingerichtet werden.
   - **Netzwerk-Hinweis**: funktioniert direkt, wenn das Gerät (Handy/PC), von dem aus die Weboberfläche geöffnet wird, im selben Subnetz wie der Broker ist (Normalfall im Heimnetz). Für Zugriff von unterwegs (z. B. Mobilfunknetz statt Heim-WLAN) müsste der Broker zusätzlich über denselben Weg erreichbar sein wie der ESP32 selbst (siehe frühere VPN-Kopplung Heimnetz↔Ferienhaus, `docs/requirements.md`) — reine Netzwerk-/Infrastrukturfrage, kein Firmware-Thema, hier nur als Hinweis festgehalten.
2. **Frontend-Ansatz: Alpine.js** (2026-08-17 entschieden). Leichtgewichtig (~15–40 KB), kein Build-Schritt, direkt als einzelne Datei in LittleFS ablegbar. Deklarative Reaktivität für Listen (Programme/Zeitplan) und Live-Updates aus eintreffenden MQTT-Nachrichten, ohne den Aufwand eines vollen SPA-Frameworks mit Build-Pipeline.
3. **Dateisystem: `LittleFS`** statt `SPIFFS` (moderner Nachfolger, bessere Eignung für mehrere benannte Dateien wie `index.html`/`style.css`/`app.js`) — betrifft `ConfigStore`s Dateizugriff, bei der Umsetzung auf Persistenz-Regression prüfen (`config.json`/`programs.json`/`schedule.json` müssen nach dem Wechsel weiterhin korrekt geladen werden).

## Umsetzung (2026-08-17)

- `ESPAsyncWebServer` + `AsyncTCP` als neue Abhängigkeit (`platformio.ini`, über den aktiv gepflegten `ESP32Async`-Fork, da der ursprüngliche `me-no-dev`-Namensraum unmaintained ist) — De-facto-Standard für nicht-blockierende ESP32-Webserver, passt zum durchgehaltenen Non-Blocking-Prinzip des Projekts. Dank Architekturentscheidung B beschränkt sich `WebManager` rein auf statisches File-Serving — keine REST-Endpoints, kein JSON-Bridging zu `ConfigStore`/`MqttManager`.
- Dateisystem-Wechsel `SPIFFS` → `LittleFS`: `board_build.filesystem = littlefs` in `platformio.ini`, `ConfigStore` komplett auf `LittleFS.h` umgestellt (11 Fundstellen, `#include` + alle `SPIFFS.`-Aufrufe).
- **Bug gefunden und behoben**: `pio run --target uploadfs` (nutzt `mklittlefs`) erzeugt ein gültiges LittleFS-Image, aber `LittleFS.begin(true)` (Auto-Format bei Mount-Problemen) erkannte es beim ersten Boot fälschlich als ungültig und formatierte automatisch neu — dabei gingen sowohl die per `uploadfs` geschriebenen Web-Dateien als auch die bereits persistierte Konfiguration (Testprogramme, Zeitplan) verloren. **Ursache war die falsche Annahme, ein Auto-Format sei die sichere Voreinstellung** — tatsächlich war genau das der Datenvernichter. Per gezielter Diagnose isoliert: mit `LittleFS.begin(false)` (kein Auto-Format) mountet dasselbe `uploadfs`-Image sauber, alle Dateien sofort sichtbar. **Fix**: `ConfigStore::begin()` nutzt jetzt dauerhaft `LittleFS.begin(false)`. Kehrseite: eine wirklich leere, nie per `uploadfs` beschriebene Partition muss einmalig vorbereitet werden, sonst schlägt der Mount fehl (loggt `"LittleFS-Mount fehlgeschlagen"`) — für dieses Projekt (ein einzelnes, bereits mehrfach geflashtes Gerät) eine unkritische Einschränkung.
- Persistenz mit dem finalen Fix erneut verifiziert: Laufzeit-Schreiben/Lesen über LittleFS funktioniert zuverlässig (Alias-Wert per MQTT gesetzt, überlebt Reboot), und die per `uploadfs` bereitgestellten Web-Dateien sind nach jedem Boot korrekt vorhanden — `data/` ist damit die alleinige Quelle für die Web-Dateien, kein Duplikat/Workaround in der Firmware nötig.
- Neue `Logger::Source::WEB` ergänzt (Nutzerwunsch) — eigene Log-Kategorie `WEB  ` für `WebManager`, analog zu `WIFI `/`MQTT `/`I2C  `/`HMI  `.
- Dashboard-Cards-CSS-Tokens (`data/style.css`) als gemeinsame Grundlage abgelegt.

## Betroffene Dateien

- `platformio.ini` (neue `lib_deps`: `AsyncTCP`, `ESPAsyncWebServer`; `board_build.filesystem = littlefs`)
- `src/WebManager.h/.cpp` (neu, reines File-Serving)
- `src/main.cpp` (Einbindung `WebManager::begin()`, kein `loop()`-Aufruf nötig — vollständig async)
- `src/ConfigStore.h/.cpp` (`SPIFFS.h`→`LittleFS.h`, alle Dateizugriffe umgestellt, `LittleFS.begin(false)` statt `begin(true)`)
- `src/Logger.h/.cpp` (neue `Source::WEB`)
- `data/index.html`, `data/style.css` (neu, alleinige Quelle — via `uploadfs` ausgeliefert)

## Test / Ergebnis

1. Seite lädt im Browser über die Geräte-IP (`http://192.168.10.33/`) — `index.html`/`style.css` liefern HTTP 200 mit korrektem Inhalt, per `uploadfs` ausgeliefert (kein Firmware-Embedding). ✅
2. Flash-Größen-Checkpoint: 41,5 % → 43,1 % (≈+45 KB durch `ESPAsyncWebServer`+`AsyncTCP`+`LittleFS`) — deutlich weniger als die grob geschätzten 200–300 KB, reichlich Marge für die Folgephasen. ✅
3. Persistenz-Regressionstest (Laufzeit-Schreiben/Lesen über LittleFS, mit dem finalen `begin(false)`-Fix): Alias-Wert per MQTT gesetzt, Reboot ausgelöst, Wert korrekt erhalten geblieben. ✅
4. `uploadfs`-Kompatibilität mit `begin(false)`: Image frisch hochgeladen, Reboot, Web-Dateien sofort korrekt ausgeliefert — kein Reformat, keine verlorenen Dateien. ✅
5. **Nebenbefund während der Fehlersuche**: das ursprüngliche `begin(true)`-Verhalten hatte einmalig die zu diesem Zeitpunkt gesetzten Testdaten (5 Testprogramme, ein Zeitplan-Eintrag) gelöscht — vorher nicht explizit angekündigt, im Nachhinein als unkritisch bestätigt (reine Testdaten). Für künftige Dateisystem-/Partitions-Änderungen als Lehre festgehalten: vorher ausdrücklich ankündigen, auch wenn die betroffenen Daten nur Testdaten sind.
