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

## KiCad-Projekt (`kicad/`)

Echtes Schaltplan-Projekt (Bauteil-Symbole, elektrische Netze, ERC-fähig) zum grafischen Nacharbeiten in KiCad — Alternative zum reinen SVG-Editing, wenn eine "richtige" EDA-Bearbeitung gewünscht ist.

- `kicad/stromlaufplan.kicad_pro` — Projekt, in KiCad öffnen.
- `kicad/stromlaufplan.kicad_sch` — Schaltplan: ESP32-C6, I2C-Level-Shifter, MCP23017, Relaismodul, gleiche Verdrahtung wie der schemdraw-Plan. Alle vier Bauteile als eigene, vollständige Symbole (`gartenwasser.kicad_sym`, per `sym-lib-table` im Projekt registriert).
- `kicad/build_kicad_sch.py` — generiert die drei Dateien oben aus denselben Pin-Listen wie `stromlaufplan.py`. Bei Pin-Änderungen dort zuerst anpassen, dann `python build_kicad_sch.py` neu laufen lassen (überschreibt manuelle KiCad-Änderungen!).

Nur ERC-Warnungen für absichtlich offene Pins (GPIO4-9, GPA0-7, NC, INTA/INTB, USB-Pins etc. — passend zum "vollständig mit allen Pins"-Ansatz), keine echten Verdrahtungsfehler.
