# Phase 21 — Web-Interface: Firmware-Update (OTA)

**Status:** ✅ Vollständig erledigt & getestet (beide Teile: WebIF-Upload + PlatformIO/ArduinoOTA)

## Ziel

OTA-Updates ohne serielle Verbindung, aus zwei unabhängigen Blickwinkeln:

1. **Wichtigster Teil (Nutzer-Priorität)**: eine WebIF-Seite, über die eine neue Firmware **ohne Entwicklungsumgebung** bereitgestellt werden kann — z. B. für andere Nutzer des Geräts, die kein PlatformIO installiert haben.
2. Der eigene Entwicklungs-Workflow (`pio run --target upload`/`uploadfs` über WLAN statt Kabel) — separater zweiter Teil, siehe „Offene Punkte" unten.

## Design-Entscheidungen (vorab abgestimmt)

- **Zwei getrennte Uploads statt eines gebündelten Archivs** (Nutzer schlug ein `.tar` vor, im Gespräch verworfen): PlatformIO erzeugt Firmware (`firmware.bin`) und Dateisystem (`littlefs.bin`) ohnehin als zwei separate Build-Artefakte. Ein eigener Tar-Parser auf dem ESP32 wäre reiner Mehraufwand ohne Gegenwert, und während der gesamten bisherigen Entwicklung wurde so gut wie nie beides gleichzeitig aktualisiert (`uploadfs` bei reinen Web-Änderungen, `upload` bei reinen Firmware-Änderungen) — ein Bundle hätte das unnötig gekoppelt.
- **Bewusste Ausnahme von Architektur B** (`docs/spec/16-webif-fundament.md`, „Browser spricht für Live-Daten direkt per MQTT-over-WebSocket, ESP32 liefert nur statische Dateien"): der Upload selbst braucht einen echten `HTTP POST`-Endpunkt, da MQTT-Payloads im Projekt auf wenige KB gedeckelt sind (`kMaxJsonPayloadSize`), Firmware/Dateisystem aber 1–2 MB groß sind.
- **Kein gebündeltes `ElegantOTA`** (ursprünglich in der Phase-16-Grobplanung als Platzhalter genannt, siehe `docs/requirements.md`) — stattdessen zwei schlanke, selbst geschriebene Endpunkte direkt auf der bereits vorhandenen ESP32-`Update`-Bibliothek, passend zur bestehenden Codebasis (keine zusätzliche Abhängigkeit).
- **Partitionsschutz kommt automatisch**: `Update.begin(size, U_SPIFFS)` sucht die Zielpartition per `SubType == spiffs` (das ist exakt `webfs`, siehe `partitions.csv`) — die separate `config`-Partition (persönliche Einstellungen: `config.json`/`programs.json`/`schedule.json`) hat einen anderen SubType (`0x40`) und wird dadurch nie getroffen. Kein Extra-Code nötig, um Nutzereinstellungen vor einem Update zu schützen.
- **Keine Authentifizierung**, **kein automatisches Rollback** bei einer fehlgeschlagenen neuen Firmware (bräuchte ESP-IDF „App Rollback", zusätzlicher Umfang) — bewusst einfach gehalten für die erste Version, passend zum bisherigen Sicherheitsniveau des Projekts (weder MQTT noch Web haben Auth). Im Fehlerfall bleibt der serielle Reflash als Fallback.

## Umsetzung

- **`Logger`**: neue `Source::OTA` — Firmware-/Dateisystem-Updates werden damit unabhängig vom Übertragungsweg (WebIF-Upload, später ArduinoOTA) einheitlich geloggt statt als `WEB` oder eine dritte, uneinheitliche Quelle.
- **`WebIF.cpp`**: zwei neue Routen `POST /api/ota/firmware` (`U_FLASH`, Ziel: jeweils inaktiver `app0`/`app1`-Slot, automatisch von `Update` ermittelt) und `POST /api/ota/filesystem` (`U_SPIFFS`, Ziel: `webfs`). Gemeinsame Kernlogik (`handleOtaChunk()`) verarbeitet die Chunks aus `ESPAsyncWebServer`s `onFileUpload`-Callback, schreibt sie per `Update.write()`, loggt Start/Erfolg (inkl. `Update.md5String()` zur Integritätsprüfung) und Fehler. Vor dem Dateisystem-Update wird `LittleFS.end()` aufgerufen (die `webfs`-Partition ist zur Laufzeit gemountet) — ein Remount ist nicht nötig, da nach einem erfolgreichen Update ohnehin neu gestartet wird.
- **Verzögerter Neustart**: `ESP.restart()` läuft nicht direkt im Upload-Callback, sondern über ein Flag, das `WebIF::loop()` (neu, alle 3 Verwaltungsfunktionen bekamen bereits eine `loop()`, WebIF bisher nicht) nach 500 ms auswertet — sonst droht die HTTP-Erfolgsantwort verloren zu gehen, weil die TCP-Verbindung durch den Neustart schon weg ist, bevor `AsyncTCP` sie tatsächlich rausgeschickt hat.
- **Neue Seite `data/ota.html`/`data/ota.js`**: zwei Karten (Firmware/Dateisystem), je Datei-Auswahl + Upload-Button + Fortschrittsbalken (`XMLHttpRequest.upload.onprogress`) + Status-Text. Bewusst **ohne** MQTT-Verbindung (einzige Seite ohne `mqtt.min.js`) — die Seite spricht ausschließlich mit dem eigenen Webserver des Geräts, eine zusätzliche WebSocket-Verbindung wäre hier nur unnötige Komplexität während eines ohnehin sensiblen Vorgangs.
- **Navigation**: neuer Tab „Update" auf allen sechs Seiten ergänzt.
- **`data/style.css`**: `.ota-grid`/`.ota-card`/`.ota-desc`/`.ota-status(-success/-error)`/`.ota-reload-card`, erste Verwendung von `h3` im Projekt (Kartenüberschrift).

## Bug gefunden und behoben: Gerät startete nach OTA-Update nicht neu

Erster End-to-End-Test über `curl` ergab HTTP 200 und ein exakt passendes `Update.md5String()` (identisch zum lokal berechneten MD5 der hochgeladenen Datei) — die Übertragung war also byte-genau korrekt. Trotzdem lieferte der Webserver danach nur noch `404` für alle Pfade, und nach einem Firmware-Update passierte serverseitig gar nichts sichtbar Neues.

**Root Cause**: `WebIF::loop()` wurde zwar implementiert, aber **nie in `main.cpp`s `loop()` eingehängt** — der Neustart-Timer wurde dadurch nie ausgewertet, `ESP.restart()` also nie aufgerufen. Für das Dateisystem-Update bedeutete das: `LittleFS.end()` (im Upload-Callback aufgerufen) blieb dauerhaft unmounted, ohne dass je ein Neustart die frisch geschriebenen Daten neu mountete — daher die 404-Flut trotz korrekt geschriebener Daten. Für die Firmware bedeutete es: `esp_ota_set_boot_partition()` (intern von `Update.end()` aufgerufen) hatte den Boot-Slot zwar umgestellt, aber ohne Neustart lief die alte Firmware einfach weiter.

**Fix**: `WebIF::loop();` in `main.cpp`s `loop()` ergänzt (ein Einzeiler). Kein Problem mit der eigentlichen Schreib-/Übertragungslogik — die war die ganze Zeit korrekt, wie die MD5-Übereinstimmung schon vor dem Fix zeigte.

## Betroffene Dateien

- `src/Logger.h`/`.cpp` (neue `Source::OTA`)
- `src/WebIF.h`/`.cpp` (Upload-Endpunkte, `loop()`)
- `src/main.cpp` (`WebIF::loop()` eingehängt)
- `data/ota.html`/`data/ota.js` (neu)
- `data/index.html`, `data/konfiguration.html`, `data/programme.html`, `data/zeitplan.html`, `data/log.html` (Navigation)
- `data/style.css` (Update-Seiten-Stile)
- `data/log.js` (`LOG_SOURCES` um `OTA` ergänzt)

## Test / Ergebnis

1. **Build/Link**: `Update`-Bibliothek bindet sauber ein, keine Kompilierfehler, RAM/Flash praktisch unverändert. ✅
2. **Dateisystem-OTA, byte-genau** (`curl -F filesystem=@littlefs.bin http://.../api/ota/filesystem`): `Update.md5String()` im Live-Log identisch zum lokal berechneten MD5 der Quelldatei. ✅
3. **Bug reproduziert**: kein Neustart nach Upload, Live-Log zeigte keine neue Boot-Sequenz, Webserver lieferte danach nur `404`. ❌ (vor Fix, Ursache: fehlendes `WebIF::loop()` in `main.cpp`)
4. **Fix verifiziert**: nach Ergänzen von `WebIF::loop();` — Dateisystem-Upload gefolgt von echtem Neustart, `curl http://.../index.html` → `HTTP 200`, Seiteninhalt (inkl. neuem „Update"-Tab) korrekt ausgeliefert. ✅
5. **Firmware-OTA End-to-End**: `curl -F firmware=@firmware.bin http://.../api/ota/firmware` → MD5-Übereinstimmung, danach vollständige neue Boot-Sequenz im Live-Log (WLAN/MQTT-Reconnect, alle State-Publishes) — Gerät läuft nachweislich auf der per OTA übertragenen Firmware. ✅
6. Zwischenzeitlich per seriellem `uploadfs` auf bekannt guten Stand zurückgesetzt, um während der Fehlersuche nicht in einem kaputten Zustand zu bleiben (Dateisystem war durch das damals fehlende `WebIF::loop()` dauerhaft unmounted). ✅
7. **Echter Nutzer-Test** (nicht nur `curl`): Firmware-Version-Feature ergänzt (`include/Version.h`, retained Topic `diagnostics/version`, Anzeige im Dashboard), damit sich ein Update eindeutig bestätigen lässt. Nutzer hat darauf selbst über die Update-Seite Firmware **und** Dateisystem hochgeladen (`pio run --target buildfs` zum Bauen ohne Flashen), neue Version kam korrekt im Dashboard an. Nutzer-Fazit: „rennt wie Teufel, OTA approved!“ ✅

## Teil 2: PlatformIO/ArduinoOTA (eigener Dev-Workflow ohne Kabel)

- **Neue `src/OTA.h`/`.cpp`**: schlanker Wrapper um die `ArduinoOTA`-Bibliothek (mDNS-Ankündigung als `gartenwasser.local`, kein Passwort — passend zum bisherigen Sicherheitsniveau des Projekts). `onStart`/`onEnd`/`onError`-Callbacks loggen über die neue `Source::OTA`, identisch zum WebIF-Upload-Pfad. `ArduinoOTA.begin()` übernimmt Neustart-nach-Erfolg selbst (`setRebootOnSuccess`, Default `true`) — anders als beim WebIF-Upload ist hier **kein** eigener verzögerter Neustart nötig, das UDP/TCP-Protokoll klärt das intern.
- **`WiFiController.cpp`**: `WiFi.setHostname("gartenwasser")` ergänzt (vor `WiFi.begin()`, wie von der API gefordert) — macht das Gerät im Router/DHCP erkennbar und ist derselbe Name, unter dem `ArduinoOTA` sich per mDNS bewirbt.
- **`platformio.ini`**: neues `[env:esp32-c6-devkitc-1-ota]`, erbt (`extends`) alles vom bestehenden Serial-Environment, überschreibt nur `upload_protocol = espota` + `upload_port` (aktuelle Geräte-IP). Aufruf: `pio run -e esp32-c6-devkitc-1-ota --target upload` bzw. `--target uploadfs`.
- **`main.cpp`**: `OTA::begin()` direkt nach dem blockierenden WLAN-Verbindungsaufbau, `OTA::loop()` in der Haupt-Loop.
- Auf Hardware verifiziert: `pio run -e esp32-c6-devkitc-1-ota --target upload` mit dem echten `firmware.bin` — vollständiger Upload (100 %-Fortschrittsanzeige), `Result: OK`/`Success`, Live-Log zeigt `PlatformIO-OTA gestartet (Firmware).`/`... abgeschlossen, Neustart folgt.`, danach sauberer Neustart (Reset-Grund `SW_CPU`) und normaler Boot mit korrekter neuer Version. Ein erster Versuch direkt nach einem frischen seriellen Flash schlug mit „No response from device" fehl (vermutlich zu knappes Timing, mDNS/UDP-Listener war da noch nicht vollständig bereit) — beim Retry auf einem bereits laufenden Gerät fehlerfrei.

## Offene Punkte

- Kein automatisches Rollback bei nicht-bootender Firmware (bewusst zurückgestellt, siehe „Design-Entscheidungen" oben) — gilt für beide OTA-Wege.
- Keine Authentifizierung auf den Upload-Endpunkten (bewusst zurückgestellt, siehe oben).
