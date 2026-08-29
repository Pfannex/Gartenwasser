# Stromlaufplan (KiCad)

Der offizielle, von Hand gepflegte Schaltplan für Kapitel 3 des Benutzerhandbuchs (`docs/manual/usermanual.md`).

- `kicad/stromlaufplan.kicad_pro` — Projekt, in KiCad öffnen.
- `kicad/stromlaufplan.kicad_sch` — der Schaltplan selbst: ESP32-C6, I2C-Level-Shifter (inkl. diskreter 5-kΩ-Pull-ups R1/R2), MCP23017. Ventil-Ausgänge (`V0`–`V5`) sind als Signal-Bezeichner an den `GPBx`-Pins herausgeführt, das externe Relaismodul ist nicht als eigenes Bauteil gezeichnet.
- `kicad/gartenwasser.kicad_sym` — die Bauteil-Symbole (ESP32-Board, MCP23017, Level-Shifter).

## Ursprung / Weiterbearbeiten

Ursprünglich per Skript (`tools/schematics/build_kicad_sch.py`) als Startgerüst erzeugt, dann von Hand in KiCad weiterverdrahtet und angepasst — der aktuelle Stand hier ist **nicht** mehr identisch mit dem Skript-Output und wird auch nicht mehr davon überschrieben. Änderungen ab jetzt: direkt in KiCad, Datei speichern.

Nach jeder Änderung, die auch ins Handbuch übernommen werden soll:
1. In KiCad: Datei → Exportieren → Grafik (SVG), oder `kicad-cli sch export svg stromlaufplan.kicad_sch`.
2. Auf den eigentlichen Inhalt zuschneiden (die A2-Seite ist meist grösstenteils leer) und als WebP speichern.
3. `docs/manual/images/schaltplan-vollstaendig.webp` ersetzen.

## Frühere schemdraw-Version

Vor der KiCad-Umstellung wurde der Plan mit dem Python-Paket `schemdraw` erzeugt (`tools/schematics/stromlaufplan.py`) — liegt dort weiterhin als Referenz, wird aber nicht mehr fürs Handbuch verwendet.
