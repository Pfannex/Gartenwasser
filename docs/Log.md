# Log — Gartenwasser

Chronologisches Entwicklungs-Log. Fachliche Spezifikation siehe [requirements.md](requirements.md), Phasen-Details siehe [spec/](spec/).

## 2026-08-14

### PlatformIO-Library-Cache defekt

`.pio/libdeps/esp32-c6-devkitc-1` war beschädigt (u.a. `GFX Library for Arduino` ohne `library.properties`/`src/`, `lvgl`/`PubSubClient`/`ArduinoJson` fehlten komplett) → `MissingPackageManifestError` beim Build. Fix: kompletten `.pio`-Ordner gelöscht (reiner Build-/Dependency-Cache, in `.gitignore`), PlatformIO installiert Abhängigkeiten beim nächsten Build sauber neu.

### Phase 2 — MQTT-Grundgerüst umgesetzt

- `MqttManager` (`src/MqttManager.h/.cpp`, neu): PubSubClient-Verbindung zum Broker aus `secrets.h`, LWT auf `gartenwasser/availability` = `offline` (retained, QoS 1), nach Connect `online` (retained). Nicht-blockierendes Reconnect-Handling analog zu `WifiManager` (15 s Intervall).
- In `main.cpp` eingebunden (`begin()` in `setup()`, `loop()` in `loop()`).

### Boot-Ablauf: WLAN + NTP blockierend, Logger auf Echtzeit

- `WifiManager::connectAndSyncTimeBlocking()` (neu): baut beim Boot zuerst die WLAN-Verbindung auf (Timeout 20 s), direkt danach NTP-Synchronisierung (`pool.ntp.org` / `de.pool.ntp.org`, Zeitzone Europe/Berlin, Timeout 10 s). Beide Wait-Routinen inkl. Timeouts/Konstanten liegen vollständig in `WifiManager.cpp`, damit `main.cpp` ein schlanker Orchestrator bleibt.
- Bei Erfolg schaltet `Logger::enableRealTime()` das Log-Zeitformat von boot-relativer Zeit (`millis()`-basiert) auf Echtzeit um (`hh:mm:ss` aus NTP, `mmm` weiterhin aus `millis()`). Schlägt WLAN oder NTP fehl, läuft der Boot mit Fehlermeldung und boot-relativer Zeit weiter (kein Hänger ohne Netz).
- `Logger::Source::SYSTEM` ergänzt (`"SYS  "`) für Boot-/Systemmeldungen wie `"Setup abgeschlossen."`.

### Getestet auf Hardware

- Geflasht per PlatformIO „Upload and Monitor“ (VS Code Task, Shortcut `Strg+Alt+U`) — läuft.

### Doku-Status nachgezogen, Git-Historie bereinigt

- Phase-2-Status in `docs/README.md`, `docs/spec/02-mqtt-grundgeruest.md` und `docs/requirements.md` auf ✅ nachgezogen.
- Lokaler `master` hatte durch ein `git commit --amend` nach dem ersten Push keine gemeinsame Historie mehr mit `origin/master` (zwei unabhängige Root-Commits). Fix: `git reset --soft origin/master`, alle Änderungen in einem neuen Commit zusammengefasst, normal gepusht (kein Force-Push nötig).
- `.claude/settings.local.json`: `git push` als Permission-Regel hinterlegt, läuft seither ohne Rückfrage.

### Phase 3 — ValveController umgesetzt

- `ValveController` (`src/ValveController.h/.cpp`, neu): `setValve(index, on)`/`getValve(index)` für V0–V5, schreibt auf MCP23017 Port B (`I2CManager::writeRegister()`) mit der Pinbelegung aus `requirements.md` (V0=B7, V1=B2…V5=B6). `begin()` setzt beim Boot alle Ventile auf AUS.
- `main.cpp`: `ValveController::begin()` in `setup()` eingebunden. Temporärer Serial-Testhandler (`v0on`…`v5on`/`v0off`…`v5off`) ergänzt — entfällt wieder mit Phase 4 (MQTT-Anbindung der Ventile).
- Getestet auf Hardware: alle sechs Ventile einzeln per Serial-Befehl geschaltet, Relais-Ausgänge laufen einwandfrei.

### Phase 4 — Ventile per MQTT umgesetzt

- `MqttManager`: abonniert `gartenwasser/V{1..5}/cmd` nach jedem (Re-)Connect, Callback parst `ON`/`OFF` und schaltet über `ValveController`. `state` (inkl. `V0`) wird retained published, nur bei tatsächlicher Änderung. Nach jedem Connect werden zusätzlich alle aktuellen Ventilzustände neu published (Resilienz bei Reconnect, siehe `requirements.md`).
- V0-Kopplung: `Vn ON` schaltet `V0` mit ein; `Vn OFF` schaltet `V0` nur aus, wenn kein anderes Ventil mehr aktiv ist (`ValveController::anyIrrigationValveActive()`, neu).
- Ungültige Topics/Payloads werden geloggt und ignoriert.
- `main.cpp`: temporärer Serial-Testhandler aus Phase 3 entfernt (durch MQTT abgelöst).
- Getestet auf Hardware: alle Ventile per MQTT einzeln/kombiniert geschaltet, V0-Kopplung verifiziert.

## 2026-08-15

### Phase 5 — Laufzeit & Restlaufzeit umgesetzt

- `ConfigStore` (`src/ConfigStore.h/.cpp`, neu): persistiert `time` je Ventil und `maxTime` in `/config.json` auf SPIFFS (ArduinoJson v6). Defaults: 5 Min./Ventil, 60 Min. `maxTime`.
- `ValveTimer` (`src/ValveTimer.h/.cpp`, neu): reiner Countdown — `start()` setzt `remaining = min(time, maxTime)`, `tick()` zählt sekündlich herunter, meldet abgelaufene Ventile über eine Bitmaske zurück.
- `MqttManager`: `V{1..5}/time/set` (validiert, Fehler werden geloggt statt übernommen), `time/state`+`time/remaining` (nicht retained) published, `main/time/maxTime` retained published. `applyValveCommand` steuert den Timer mit (Start bei ON, Reset bei OFF).
- **Wichtig:** der sekündliche Countdown/die Auto-Abschaltung läuft bewusst **vor** dem WLAN-Verbindungscheck in `MqttManager::loop()`, also unabhängig von WLAN/MQTT — sonst würde ein Verbindungsausfall den Countdown einfrieren und ein Ventil könnte unbegrenzt offen bleiben (Anforderung „läuft lokal/autonom weiter“ aus `requirements.md`). In der Phase-5-Spec nicht explizit erwähnt, aber zwingend notwendig.
- `main/time/maxTime` ist vorerst nur publish-only (Default 60 Min.) — der Set-Weg (`main/time/set`-JSON) kommt laut Spec erst in Phase 11.
- Getestet auf Hardware: `time/set` übersteht Neustart (Persistenz bestätigt), Ventile schalten bei `00:00` automatisch ab.

## Offene Punkte / nächste Schritte

- Phase 6 (Automatik-Flag je Ventil, `auto/set`/`auto/state`) ist der nächste inhaltliche Schritt.
