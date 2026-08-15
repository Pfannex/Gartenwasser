# Phase 12 — Aufräumen/Refactoring

**Status:** ✅ Erledigt (Code-Review-Pass), Regressionstest steht vor jedem Release weiter aus

## Ziel

Code-Qualität und Wartbarkeit vor dem produktiven Einsatz sicherstellen.

## Bereits vorgezogen (vor Phase 2)

Ein Teil dieser Phase wurde bereits umgesetzt, um auf einer sauberen Basis mit Phase 2 zu starten:

- `Logger`-Klasse für einheitliches Log-Format (`src/Logger.h/.cpp`).
- `HmiManager` (Display/Touch/LVGL) aus `main.cpp` ausgelagert.
- `I2CManager` (I2C-Bus-Scan, MCP23017-Grundsetup) aus `main.cpp` ausgelagert.
- `WifiManager` nutzt jetzt `Logger` statt direktem `Serial`.
- `main.cpp` ist ein schlanker Orchestrator.

## Umgesetzt (2026-08-15, Code-Review-Pass nach Phase 11)

- **Veraltete Kommentare korrigiert**: `main.cpp`s Datei-Header nannte nur 3 von inzwischen 11 Klassen; der `ConfigStore::begin()`-Kommentar sagte noch „später auto, alias" obwohl beides seit Phase 6/9 fertig ist.
- **Doppelt gepflegte JSON-Puffergröße zusammengeführt**: `ConfigStore` und `MqttManager` hatten je eine eigene `768`-Konstante für die maximale Config-JSON-Größe. Jetzt einmal öffentlich in `ConfigStore::kJsonCapacity`, von `MqttManager` wiederverwendet — verhindert zukünftiges Auseinanderlaufen (z. B. wenn Aliase mal länger werden dürfen).
- **Topic-Puffergrößen vereinheitlicht**: Statt individuell geschätzter `char topic[24]`/`char topic[32]` an über einem Dutzend Stellen jetzt eine gemeinsame, großzügige `kTopicBufferSize = 48` in `MqttManager.cpp`. Genau aus einer solchen zu knapp bemessenen Einzelgröße kam bereits einmal ein Bug (`V{n}/auto/state` wurde abgeschnitten, siehe `docs/Log.md`, 2026-08-15) — eine gemeinsame, großzügige Konstante macht diese Fehlerklasse strukturell unwahrscheinlicher.
- **`MqttManager`/`Diagnostics`/`Logger`-Kopplung**: war hier als offener Punkt gelistet, ist aber bereits in Phase 8 umgesetzt (`Logger::setErrorCallback()`) — Punkt erledigt, kein weiterer Aufwand nötig.
- **Speicher-Check**: aktueller Build (nach Phase 11, vollständige Implementierung bis auf HA-Discovery/Touch-UI/Backlog-Phasen): RAM 33,6 % (110.108 / 327.680 Byte), Flash 41,0 % (1.288.653 / 3.145.728 Byte). Deutlich Luft nach oben für die verbleibenden Phasen (13–15, ggf. 10).

## Bewusst nicht umgesetzt: Broker-/Topic-Präfix zentral konfigurierbar

Entscheidung: **nicht** umgesetzt. Begründung: `"gartenwasser"` ist über ein Dutzend Stellen in `MqttManager.cpp` als Literal in Topic-Strings verteilt; eine echte Zentralisierung würde entweder alle Topics zur Laufzeit aus einem Präfix zusammenbauen (Overhead an jeder Publish-/Subscribe-Stelle) oder eine Compile-Zeit-Stringverkettung erfordern (mit dem verfügbaren C++-Standard/Toolchain unhandlich). Der Nutzen wäre rein hypothetisch — es handelt sich um ein Einzelgerät-Hobbyprojekt, bei dem der Topic-Präfix in der Praxis nie geändert werden muss. Der Aufwand/Risiko-Vergleich (viele Stellen in bereits hardware-getestetem Code anfassen, für ein Feature, das nie gebraucht wird) spricht dagegen. Falls sich das ändert (z. B. mehrere Geräte am selben Broker), kann das jederzeit gezielt nachgezogen werden.

## Test-Checkliste für zukünftige Firmware-Updates

Manueller Regressionstest vor jedem Release (Hardware + `mosquitto_sub -t gartenwasser/# -v`):

1. **Boot**: Trennlinie erscheint im Serial-Monitor, WLAN verbindet, NTP synchronisiert (Log wechselt auf Echtzeit), MQTT verbindet, `availability` = `online`.
2. **Ventile manuell**: `V{n}/cmd ON`/`OFF` für alle V1–V5 → `state` + V0-Kopplung korrekt (V0 an sobald ein Ventil an, aus erst wenn alle aus).
3. **Laufzeit**: `V{n}/time/set` setzt `time/state`, `time/remaining` zählt beim Einschalten korrekt herunter, Ventil schaltet bei `00:00` automatisch ab, `maxTime` deckelt korrekt.
4. **Automatik-Flag**: `V{n}/auto/set` → `auto/state` retained, übersteht Neustart.
5. **Automatik-Sequenz**: `main/cmd ON` durchläuft alle `auto=ON`-Ventile der Reihe nach, `activeValve`/`remainingTotal` korrekt, manuelles `OFF` des aktiven Ventils schaltet weiter, `main/cmd OFF` bricht sofort ab, Restlaufzeiten wie erwartet (0 während laufender Sequenz für durchgelaufene Ventile, `time` nach Sequenz-Ende).
6. **Diagnostics**: I2C-Verbindung kurz trennen → `i2cStatus` = `error` + `lastError` gesetzt, wieder verbinden → `ok`.
7. **Alias**: `V{n}/alias/set` (inkl. `V0`) → `alias` retained, übersteht Neustart, Sonderzeichen/Umlaute funktionieren, zu lange/ungültige Werte werden abgelehnt.
8. **Konfiguration per JSON**: `main/config/set` mit Teil- und Vollupdate, unbekannte Keys werden ignoriert, `main/config/state` liefert sofort (retained) den aktuellen Stand.
9. **Resilienz**: WLAN/MQTT kurz trennen → laufende Ventile/Sequenz laufen lokal weiter, nach Reconnect werden alle retained States neu publiziert.
10. **Persistenz**: Board neu starten → `time`/`auto`/`alias`/`maxTime` bleiben erhalten, alle Ventile sind AUS, keine Automatik-Sequenz startet automatisch neu.

## Test

- Vollständiger Regressionsdurchlauf aller MQTT-Topics laut `docs/requirements.md` — bereits einzeln je Phase auf Hardware verifiziert (siehe `docs/Log.md`); ein zusammenhängender Gesamtdurchlauf nach der Checkliste oben steht als Vorbereitung für den produktiven Einsatz noch aus.
- HA-Dashboard-Kontrolle: entfällt vorerst — Phase 10 (Home Assistant) wurde bewusst ans Ende der Bearbeitungsreihenfolge gestellt (siehe `docs/requirements.md`, Entscheidungshistorie 2026-08-15), erst relevant sobald Phase 10 ansteht.
