# Phase 18 — Web-Interface: Konfiguration bearbeiten

**Status:** ✅ Erledigt & getestet

## Ziel

`time`/`auto`/`alias`/`maxTime` je Ventil (Datenmodell aus Phase 11, `main/config/*`) im Browser editierbar machen — einfachstes Datenmodell des Web-Interfaces, damit der Schreibpfad (im Gegensatz zum reinen Lesen aus Phase 17) zuerst am simpelsten Fall erprobt wird, bevor Phase 19/20 komplexere Datenmodelle (Listen, Teilmengen-Semantik, typabhängige Felder) hinzufügen.

## Voraussetzungen

- Phase 17 (Status-Dashboard) ✅ — Lesepfad der gewählten Architektur bereits verifiziert

## Design (vorab per Artefakt abgestimmt)

Vor der Umsetzung ein visueller Vorschlag vorgelegt (gleiche „Dashboard Cards“-Tokens wie die Hauptseite), über mehrere Runden verfeinert:

- **`maxTime` bleibt ein vom Nutzer gesetztes Feld**, kein abgeleiteter Wert. Zwei automatische Vorschläge (Summe aller Laufzeiten + 5 min, dann längste Einzel-Laufzeit + 5 min als abgeleiteter, schreibgeschützter Wert) wurden verworfen — der Nutzer entschied sich zurück auf ein bewusst manuell gesetztes Failsafe: „der user muss maxtime einstellen! berücksichtige maxtime immer“.
- Stattdessen macht die UI sichtbar, **wenn** `maxTime` eine Ventil-Laufzeit deckelt: die Laufzeit erscheint gelb (`--state-warning`) mit Zusatzhinweis „→ X min effektiv“, sobald `time > maxTime`. Die tatsächliche Deckelung selbst ist keine neue Arbeit — `ValveTimer` erzwingt `min(time, maxTime)` bereits firmwareseitig, `V{n}/time/remaining` zeigt entsprechend schon den korrekten Wert; die Konfig-Seite muss nur warnen, nicht selbst rechnen.
- Bestätigungs-Feedback pro Feld: statt eines Textbausteins („✓ gespeichert“, verworfen — „verwirrt“) färbt sich ein bearbeitetes Feld beim Verlassen sofort **rot** (`pending`, noch nicht per Geräte-Echo bestätigt) und blendet weich zur normalen Textfarbe zurück, sobald die Bestätigung (`.../state`-Echo) eintrifft. Bleibt ein Feld dauerhaft rot, wurde der Wert vom Gerät abgelehnt (z. B. ungültiger Alias) — kein Sonderfall nötig, ergibt sich automatisch aus dem Mechanismus.
- Zahlenfelder: mehr Abstand zwischen Ziffern und den nativen Spin-Buttons (`padding-right`, `::-webkit-inner-spin-button { margin-left }`), nach Nutzer-Feedback zu eng wirkender Pfeile.

## Umsetzung (2026-08-17)

- **Neue Seite `data/konfiguration.html`**: eigenständiges HTML-Dokument (kein SPA-Routing), gleiche Kopfzeile/Navigation wie `index.html`, Alpine-Root `x-data="konfiguration()"`. Zwei Karten: „Globale Einstellungen“ (`maxTime`) und „Ventile“ (Zeilenliste `V0`–`V5`, `V0` nur mit Alias-Feld, `V1`–`V5` zusätzlich mit Laufzeit-Zahlenfeld und Automatik-Toggle).
- **Neue `data/konfig.js`**: eigene Alpine-Komponente `konfiguration()`, eigene `mqtt.connect()`-Verbindung (analog `app.js`, gleiche feste Broker-Adresse `ws://192.168.1.123:9001/mqtt`), abonniert `gartenwasser/#`. Pflegt je Ventil `{alias, time, auto, aliasPending, timePending}` plus globales `maxTime`/`maxTimePending`.
  - `handleMessage()` erweitert das aus Phase 17 bekannte Muster um `V{n}/time/state` (bisher nur `time/remaining` behandelt) und `main/time/maxTime`.
  - Schreibpfade: `setAlias()`/`setTime()` (an `@blur` gebunden) → `V{n}/alias/set`/`V{n}/time/set`; `toggleAuto()` (an Klick auf den Switch gebunden) → `V{n}/auto/set`; `setMaxTime()` → `main/config/set` mit `{"maxTime": n}` (kein eigenes `main/time/set`, wie in `docs/requirements.md` dokumentiert — `main/time/maxTime` ist nur publish-only).
  - `isCapped(v)` (`maxTime > 0 && v.time > maxTime`) steuert die gelbe Warnfarbe + `capLabel()`-Hinweistext; `aliasClass()`/`timeClass()`/`maxTimeClass()` liefern die CSS-Klasse (`pending` rot > `time-exceeded` gelb > normal), Priorität verhindert Klassenkonflikte während ein Feld gleichzeitig bearbeitet und gedeckelt ist.
- **`data/style.css`** ergänzt: neuer Token `--state-warning` (Bernstein, hell/dunkel), `.global-row`/`.field-input-group`/`.valve-row`/`.field-col`/`.laufzeit-group`/`.switch`/`.switch-col`/`.cap-note`, Basisstile für `input[type=text|number]` (bisher nicht vorhanden, da Phase 17 rein lesend war), `input.pending`/`input.time-exceeded`.
- **`data/index.html`**: Navigationseintrag „Konfiguration“ von `class="soon"` auf echten Link (`href="konfiguration.html"`) umgestellt.
- Keine Firmware-Änderung nötig — alle verwendeten Topics (`V{n}/alias/set`, `V{n}/time/set`, `V{n}/auto/set`, `main/config/set`) existieren bereits seit Phase 5/9/11.

## Betroffene Dateien

- `data/konfiguration.html` (neu)
- `data/konfig.js` (neu)
- `data/style.css` (ergänzt: `--state-warning`, Formular-/Zeilen-Stile)
- `data/index.html` (Navigation)

## Test / Ergebnis

1. **Dateiauslieferung**: `konfiguration.html`/`konfig.js` per `curl` mit HTTP 200 und korrektem Inhalt geprüft. ✅
2. **Schreibpfad ohne Browser** (`paho-mqtt`, `transport="websockets"`, simuliert exakt das, was ein Feld-`blur()`/Toggle-Klick im Browser auslöst):
   - `V3/alias/set` → `V3/alias` echot den neuen Wert. ✅
   - `V3/time/set` → `V3/time/state` echot den neuen Wert. ✅
   - `V3/auto/set` (Toggle) → `V3/auto/state` echot den neuen Wert. ✅
   - `main/config/set {"maxTime": 12}` → `main/time/maxTime` echot `12`. ✅
   - Laufzeit über `maxTime` gesetzt (`V3/time/set 20` bei `maxTime=12`) → `V3/time/state` zeigt weiterhin den eingestellten Wert `20` (Deckelung wirkt erst beim tatsächlichen Einschalten auf `time/remaining`, nicht auf den gespeicherten Sollwert) — bestätigt, dass die Konfig-Seite den vollen `time`-Wert anzeigen und separat davor warnen muss, statt ihn selbst zu kappen. ✅
   - Testdaten danach zurückgesetzt (`V3` = „Kübelpflanzen“/8 min/`auto=false`, `maxTime` = 60, Firmware-Default).
3. **Im Browser** (Nutzer): Seite lädt korrekt, Felder vorbefüllt, Bearbeiten + Verlassen eines Feldes färbt es sofort rot und blendet nach Bestätigung weich zur normalen Textfarbe zurück, gelbe Deckelungs-Warnung erscheint wie erwartet. ✅ („passt alles“)

Damit ist der erste Schreibpfad des Web-Interfaces (Architektur B: Browser publiziert direkt per MQTT-over-WebSocket, keine eigene Server-Logik im `WebManager`) vollständig erprobt — Grundlage für die komplexeren Datenmodelle in Phase 19 (Programme) und Phase 20 (Zeitplan).

## Nachtrag (2026-08-18): Automatik-Toggle entfernt, "MANUELL"-Konsistenz

Nach dem Umsetzen von Phase 19 (Programme) stellte der Nutzer eine grundsätzliche Frage: da eine Programm-Aktivierung `time`/`auto` ohnehin überschreibt und `startSequence()` das gewählte Programm vor jedem Start zur Drift-Vermeidung erneut anwendet (siehe `docs/spec/14-programme.md`, Kernentscheidung 3), hätte eine manuelle `auto`-Änderung auf dieser Seite beim nächsten Start ohnehin wieder stillschweigend verworfen werden — Details/Beispiel siehe `docs/spec/14-programme.md`, Nachtrag 2026-08-18.

**Entscheidung** (Nutzer): `auto` ist ausschließlich über Programme setzbar, `time`/`alias` bleiben hier editierbar (steuern die manuelle Einzelventil-Laufzeit, unabhängig von Programmen). Umgesetzt:

- **Automatik-Toggle entfernt**: die `.switch`-Spalte je Ventilzeile (`data/konfiguration.html`) sowie `konfig.js`s `toggleAuto()` komplett gestrichen, nicht nur versteckt. Die Ventil-Badge-Farbe (grün/grau) bleibt als reine, nicht editierbare Information stehen — zeigt weiterhin, welche Ventile aktuell zur Automatik gehören.
- Hinweistext ergänzt: „Automatik wird ausschließlich über Programme gesetzt — eine hier geänderte Laufzeit setzt das aktive Programm auf „MANUELL“ zurück“.
- Firmwareseitig (siehe `docs/spec/14-programme.md`, Nachtrag): jede direkte `time`-Änderung über diese Seite löst jetzt `MqttManager::publishConfigStateAndClearProgram()` aus, setzt also `activeProgram` auf `0` zurück.
- Totes CSS aufgeräumt: `.switch-col` (nur noch von der entfernten Spalte genutzt) sowie das nie verwendete `button.ghost` (Rest aus Phase 16/17-Grundgerüst) entfernt.

Auf Hardware verifiziert (zusammen mit dem Firmware-Test in `docs/spec/14-programme.md`) und vom Nutzer im Browser bestätigt.
