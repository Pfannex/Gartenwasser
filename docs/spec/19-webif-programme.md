# Phase 19 — Web-Interface: Programme verwalten

**Status:** ✅ Erledigt & getestet

## Ziel

Volle Editier-Oberfläche für Bewässerungsprogramme (bis zu 32, Phase 14, `main/programs/*`) — Liste + Formular, erste wirklich komplexe UI des Web-Interfaces mit Hinzufügen/Entfernen statt nur Werte ändern (Unterschied zu Phase 18).

## Voraussetzungen

- Phase 18 (Konfiguration bearbeiten) ✅ — Schreibpfad bereits verifiziert, hier zusätzlich Listen-/Array-Semantik

## Design (vorab per Artefakt abgestimmt)

Vor der Umsetzung ein visueller Vorschlag vorgelegt (gleiche „Dashboard Cards“-Tokens, Ventilzeilen-Muster aus Phase 18 wiederverwendet), über mehrere Runden verfeinert:

- **Programmliste als Karten**: Name, „Aktiv“-Badge, Zusammenfassung der enthaltenen Ventile mit Laufzeit (z. B. „V1 Apfel 10min · V2 Beete 10min“), Laufzeiten über `maxTime` gelb markiert (dieselbe Warnlogik wie Phase 18).
- **Aktionen pro Karte**: Aktivieren (→ `main/program/cmd <index>`), Bearbeiten (klappt einen Editor in der Karte auf), Löschen (zweistufige Inline-Bestätigung statt Browser-`confirm()`-Dialog).
- **Editor**: Name-Feld, je Ventil V1–V5 eine Zeile mit Automatik-Switch + Laufzeit (deaktiviert, wenn Switch aus) — nur Ventile mit Automatik EIN werden Teil des Programms.
- **„+ Neues Programm“**: gestrichelte Karte (wie die reservierten V6/V7-Kacheln im Dashboard), öffnet denselben Editor leer.
- **„Kein Programm aktivieren“**: kleiner Link oberhalb der Liste, nur sichtbar wenn aktuell ein Programm gewählt ist.
- Zähler „(N von 32)“ macht das `kMaxPrograms`-Limit sichtbar.
- **Nachjustierung nach erstem Blick**: die obere Aktionsleiste (Aktivieren/Bearbeiten/Löschen) blieb ursprünglich auch im Bearbeitungsmodus sichtbar — vom Nutzer als überflüssig/verwirrend bewertet, da der Editor bereits eigene Abbrechen/Speichern-Buttons hat („der Bearbeitungsmodus ist wie ein eigenes modales Fenster, nur OK/Abbrechen sind aktiv“). Korrigiert: die komplette Aktionsleiste blendet sich während der Bearbeitung aus (`x-show="editingIndex !== idx"`), Schließen geht danach ausschließlich über Abbrechen/Speichern. Aktivieren ist erst nach dem Schließen wieder möglich.

## Umsetzung (2026-08-18)

- **Neue Seite `data/programme.html`**: eigenständiges HTML-Dokument, gleiche Kopfzeile/Navigation wie die anderen Seiten, Alpine-Root `x-data="programme()"`.
- **Neue `data/programme.js`**: eigene Alpine-Komponente, eigene `mqtt.connect()`-Verbindung. Datenmodell: `programs[]` (Array aus `{name, time, auto}`), `activeProgram`, `valveAlias{1..5}`, `maxTime`, `editingIndex`/`editDraft` (Arbeitskopie während der Bearbeitung), `confirmDeleteIndex` (zweistufige Löschbestätigung mit 2,5 s Auto-Reset).
  - `publishPrograms()` schickt **immer** `programs` **und** `activeProgram` zusammen (nicht nur `programs`) — Begründung: beim Löschen eines Programms vor dem aktiven verschieben sich dessen Array-Indizes (lokal nachgeführt: `activeProgram` wird dekrementiert bzw. auf `0` gesetzt, falls das aktive Programm selbst gelöscht wurde), `main/programs/set` repliziert sonst (Teil-Update-Prinzip) nur `programs` und ließe den jetzt falschen `activeProgram`-Index unverändert im Gerät stehen.
  - Aktivieren/Deaktivieren nutzt bewusst die schlanke `main/program/cmd <index>` statt `main/programs/set` (kein Array nötig für eine reine Indexwahl).
  - `programDetail(p)` baut die Ventil-Zusammenfassung inkl. gelber Deckelungs-Markierung als HTML-String (`x-html`), Logik/Formel identisch zu Phase 18.
- **`data/style.css`** ergänzt: `.program-list`/`.program-card`/`.program-head`/`.badge-active`/`.program-actions`/`.program-detail`, Button-Varianten `button.small`/`.btn-secondary`/`.btn-primary`/`.btn-danger(.confirm)`, `.program-add-card` (gestrichelt), `.editor`/`.editor-name-row`/`.editor-footer`, `.valve-row-name` (Ventilname im Editor, read-only). `.valve-row`/`.valve-badge`/`.switch`/`.laufzeit-group`/`.cap-note` aus Phase 18 unverändert wiederverwendet (identisches Grid, gleiche Spaltenreihenfolge).
- **`data/index.html`**: Navigationseintrag „Programme“ von Platzhalter auf echten Link (`href="programme.html"`) umgestellt, „Ändern“-Button in der „Aktives Programm“-Karte (bisher deaktiviert, „Folgt in Phase 19“) jetzt ein echter Link dorthin.
- Totes CSS aufgeräumt: `button.ghost` (einzige Verwendung war der jetzt aktive „Ändern“-Link).
- Keine Firmware-Änderung nötig für die Grundfunktion — alle verwendeten Topics (`main/programs/set`/`state`, `main/program/cmd`/`state`) existieren bereits seit Phase 14. (Eine Firmware-Änderung kam unmittelbar danach als eigener Nachtrag hinzu, siehe `docs/spec/14-programme.md`.)

## Betroffene Dateien

- `data/programme.html` (neu)
- `data/programme.js` (neu)
- `data/style.css` (ergänzt: Programmkarten-/Editor-Stile)
- `data/index.html` (Navigation, „Ändern“-Button aktiviert)

## Test / Ergebnis

1. **Dateiauslieferung**: `programme.html`/`programme.js` per `curl` mit HTTP 200 geprüft. ✅
2. **Schreibpfad ohne Browser** (`paho-mqtt`, `transport="websockets"`):
   - Neues Programm per `main/programs/set` (Array-Replace, bestehende Programme + neuer Eintrag) angehängt → `main/programs/state` zeigt es korrekt. ✅
   - `main/program/cmd <neuer Index>` → `main/program/state` zeigt den neuen Namen, `main/programs/state.activeProgram` stimmt. ✅
   - Testprogramm wieder per `main/programs/set` entfernt (Array ohne den Eintrag, `activeProgram` explizit auf den ursprünglichen Wert zurückgesetzt) → Ausgangszustand exakt wiederhergestellt (Byte-für-Byte-JSON-Vergleich). ✅
3. **Im Browser** (Nutzer): Liste, Bearbeiten/Anlegen/Löschen/Aktivieren funktionieren, Deckelungs-Warnung erscheint korrekt. Ein Detail nachjustiert (siehe „Design“ oben: Aktionsleiste blendet sich im Bearbeitungsmodus jetzt aus). Danach bestätigt: „sieht gut aus“.

Direkt im Anschluss ergab eine Nutzerfrage („macht eine separate Konfigurationsseite für `time`/`auto` überhaupt noch Sinn, wenn Programme das ohnehin überschreiben?“) eine grössere Konsistenz-Überarbeitung über Phase 14/17/18/13 hinweg (manuelle Änderungen setzen jetzt auf „MANUELL“ zurück, Automatik nur noch über Programme) — Details siehe `docs/spec/14-programme.md`, Nachtrag 2026-08-18.
