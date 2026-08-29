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

Rechts-Pins sind wie beim echten DIP/SOIC-Gehäuse "umgewickelt" nummeriert (Pin 1 oben links, runter, dann rechts wieder hoch) — beim MCP23017 stimmt das jetzt mit dem Microchip-Datenblatt überein (Pin 15=A0 unten rechts, Pin 28=GPA7 oben rechts). Elektrische Pin-Typen sind semantisch gesetzt (power_in für VDD/VSS/GND/VBUS/3V3, input für A0-A2/RESET/IN0-5, output für INTA/INTB/OUT0-5, no_connect für NC1/NC2, sonst bidirektional).

Erwartete ERC-Meldungen (keine echten Fehler):
- "nicht verbunden" für absichtlich offene Pins (GPIO4-9, GPA0-7, USB-Pins etc. — passend zum "vollständig mit allen Pins"-Ansatz).
- "power pin not driven" für die power_in-Pins, weil Stromversorgung hier über Global Labels (`+5V`/`+3V3`/`GND`) statt über echte KiCad-Power-Symbole läuft — ERC verlangt dafür einen "power_out"-Pin auf dem Netz. Bei Bedarf in KiCad die Global Labels gegen die Standard-Symbole `power:+5V`/`power:+3V3`/`power:GND` tauschen, dann verschwindet die Meldung.
