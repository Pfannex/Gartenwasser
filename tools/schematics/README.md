# Stromlaufpläne (schemdraw)

Erzeugt die Schaltplan-Grafiken für `docs/manual/usermanual.md` Kapitel 3.

- `stromlaufplan.py` → `stromlaufplan.svg` — vollständiger Plan (ESP32-C6, I2C-Level-Shifter, MCP23017, Relaismodul, alle Pins).
- `levelshifter_prinzip.py` → `levelshifter_prinzip.svg` — Wirkprinzip eines Level-Shifter-Kanals (Detailgrafik).

## Bearbeiten

Die `.svg`-Dateien sind reine Vektorgrafiken und lassen sich direkt von Hand in **Inkscape** (oder einem anderen SVG-Editor) nacharbeiten — Linien, Beschriftungen etc. sind einzeln fassbare Objekte.

Alternative: Parameter im jeweiligen `.py`-Skript ändern und neu rendern:

```
pip install schemdraw
python stromlaufplan.py
python levelshifter_prinzip.py
```

## Ins Handbuch übernehmen

Nach dem Bearbeiten der SVG (händisch oder per Skript) als WebP exportieren und `docs/manual/images/schaltplan-vollstaendig.webp` bzw. `docs/manual/images/levelshifter-prinzip.webp` ersetzen. In Inkscape: Datei → Exportieren als PNG, dann z.B. mit Pillow (`Image.open(...).save(..., 'webp')`) nach WebP konvertieren.

## KiCad-Projekt

**Umgezogen nach [`docs/schematics/kicad/`](../../docs/schematics/kicad/)** — das ist die für Kapitel 3 des Handbuchs tatsächlich genutzte Version (von Hand in KiCad weiterverdrahtet). Details dort in `docs/schematics/README.md`.

Der ursprüngliche Generator (`build_kicad_sch.py`, hat das KiCad-Projekt als Startgerüst erzeugt) wurde entfernt, da er den aktuellen, von Hand weiterbearbeiteten Stand ohnehin nicht mehr regeneriert hätte.
