# Phase 20 — Web-Interface: Zeitplan verwalten

**Status:** ✅ Erledigt & getestet

## Ziel

Editor für Zeitplan-Einträge (Phase 15, `main/schedule/*`) — komplexestes Datenmodell des Web-Interfaces (typabhängige Felder `daily`/`weekly`/`once`, Programm-Referenz per Name). Zugleich der eigentliche fachliche Auslöser für das gesamte Web-Interface-Vorhaben: die Touch-UI-Zeitplanbedienung wurde verworfen, weil das 172×320px-Display dafür zu klein ist (siehe `docs/spec/13-touch-ui.md`).

## Voraussetzungen

- Phase 19 (Programme verwalten) ✅ — Programm-Auswahl für die `program`-Referenz wird hier benötigt

## Design (vorab per Artefakt abgestimmt)

Vor der Umsetzung ein visueller Vorschlag vorgelegt (gleiche „Dashboard Cards“-Tokens, Karten-/Editor-Muster aus Phase 19 wiederverwendet), über eine Runde verfeinert:

- **Globaler Schalter „Zeitplan aktiv“** oben (→ `main/schedule/cmd`), mit Erklärtext für den Urlaubsmodus-Fall.
- **Eintragsliste als Karten**: Programmname als Headline (Zeitplan-Einträge haben kein eigenes `name`-Feld, `program` ist der einzige Anker, siehe `docs/spec/15-wochenplan.md`), farbcodiertes Trigger-Badge (Täglich/Wöchentlich/Einmalig), Kurzbeschreibung darunter, „Pausiert“-Badge + abgedunkelte Karte bei `enabled: false`.
- **Zwei bisher nirgends visuell behandelte Warnzustände** sichtbar gemacht: abgelaufener `once`-Termin (Datum in der Vergangenheit, gelb markiert) und „Programm nicht gefunden“ (referenzierter Programmname existiert nicht mehr — laut Spec noch offenes Verhalten, „vermutlich ignorieren + loggen“; die UI macht das jetzt zumindest sichtbar).
- **Editor** (identisches „modales“ Verhalten wie Phase 19: obere Aktionsleiste blendet sich während der Bearbeitung aus, nur Abbrechen/Speichern aktiv): Programm-Dropdown, dreiteiliger Trigger-Umschalter (Täglich/Wöchentlich/Einmalig), je nach Typ Wochentag-Chips oder Datumsfeld, Uhrzeit, „Eintrag aktiv“-Schalter.
- **„+ Neuer Eintrag“**: gestrichelte Karte wie bei Programmen, verschwindet bei 16 Einträgen (`kMaxScheduleEntries`).
- Nutzer-Feedback nach erstem Blick: eigener Button „Abgelaufene Timer löschen“ statt eines unauffälligen Textlinks für die Cleanup-Funktion (`main/schedule/cleanup`).

## Umsetzung (2026-08-18)

- **Neue Seite `data/zeitplan.html`**: eigenständiges HTML-Dokument, gleiche Kopfzeile/Navigation, Alpine-Root `x-data="zeitplan()"`.
- **Neue `data/zeitplan.js`**: eigene Alpine-Komponente, eigene `mqtt.connect()`-Verbindung. Datenmodell: `entries[]` (aus `main/schedule/state`), `globalEnabled`, `programNames[]` (aus `main/programs/state`, für das Dropdown), `editingIndex`/`editDraft`, `confirmDeleteIndex`.
  - Anders als bei Programmen (Phase 19) referenziert `program` hier einen **Namen, keinen Index** — ein Löschen/Bearbeiten verschiebt also nichts anderes, `publishSchedule()` kann `schedule` unabhängig von `enabled` (global) schicken (kein atomares Zusammen-Senden nötig, im Unterschied zu `programme.js`s `publishPrograms()`).
  - `nextOccurrence()`-Logik (Wiederkehr-Berechnung für `daily`/`weekly`/`once`) wurde bewusst **nicht** hier gebaut, sondern direkt in `app.js` für die neue Dashboard-Karte — siehe Nachtrag zu `docs/spec/17-webif-dashboard.md`.
- **`data/style.css`** ergänzt: `.type-badge` (+ Typ-Farben), `.paused-badge`, `.entry-name(.broken)`, `.entry-detail(.expired/.broken-note)`, `select`/`input[type=time|date]`-Basisstile, `.segmented` (Trigger-Umschalter), `.weekday-row`/`.weekday-chip`, `.active-toggle-row`, `.editor-row`/`.editor-grid`. Karten-/Listen-/Button-/Editor-Grundgerüst (`.program-list`, `.program-card`, `.program-head`, `.program-actions`, `.program-add-card`, `button.small` u. a.) bewusst von Phase 19 wiederverwendet statt dupliziert — deckungsgleiche Optik, gleiche Konvention wie bereits zwischen Phase 18 und 19 etabliert.
- **`data/index.html`/`konfiguration.html`/`programme.html`**: Navigationseintrag „Zeitplan“ von letztem verbliebenem `class="soon"`-Platzhalter auf echten Link umgestellt — damit ist die komplette Hauptnavigation jetzt aktiv, totes `nav.tabs a.soon`-CSS entfernt.
- Keine Firmware-Änderung nötig — alle verwendeten Topics (`main/schedule/set`/`state`/`cmd`/`cleanup`) existieren bereits seit Phase 15.

## Betroffene Dateien

- `data/zeitplan.html` (neu)
- `data/zeitplan.js` (neu)
- `data/style.css` (ergänzt: Zeitplan-spezifische Stile, `.soon`-Regel entfernt)
- `data/index.html`, `data/konfiguration.html`, `data/programme.html` (Navigation)

## Test / Ergebnis

1. **Dateiauslieferung**: `zeitplan.html`/`zeitplan.js` per `curl` mit HTTP 200 geprüft. ✅
2. **Schreibpfad ohne Browser** (`paho-mqtt`, `transport="websockets"`):
   - Neuer `daily`-Eintrag per `main/schedule/set` (Array-Replace) angehängt → `main/schedule/state` zeigt ihn korrekt. ✅
   - `weekly`-Eintrag mit `weekdays` gesetzt → korrekt im State. ✅
   - `main/schedule/cmd OFF`/`ON` → `enabled` (global) wechselt entsprechend. ✅
   - Abgelaufener `once`-Testeintrag gesetzt, `main/schedule/cleanup` gesendet → nur der abgelaufene Eintrag wird entfernt. ✅
   - Ausgangszustand (leerer Zeitplan) exakt wiederhergestellt. ✅
3. **Im Browser** (Nutzer): Liste/Bearbeiten/Anlegen/Löschen/Cleanup/globaler Schalter funktionieren, Trigger-Typen und Wochentage lassen sich wählen. Ein Detail nachjustiert (Cleanup-Button statt Textlink, siehe „Design“ oben).

Direkt im Anschluss ergab ein Nutzerwunsch („auf der Hauptseite wäre noch ein Status für den nächsten Programmstart nützlich“) eine Erweiterung von Phase 17 (neue „Nächster Termin“-Anzeige, später ins Hero-Feld verschoben) — siehe `docs/spec/17-webif-dashboard.md`, Nachtrag.
