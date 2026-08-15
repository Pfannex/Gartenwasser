# Phase 15 — Zeitplan / Scheduler (Tages- und Wochenplan)

**Status:** 📋 Backlog, grob skizziert — Details bei Umsetzung klären

## Ziel

Automatisch und autonom Bewässerungsprogramme (Phase 14) zu konfigurierten Zeitpunkten auslösen (`main/cmd ON`-Äquivalent) — ohne manuellen Eingriff. Tages- und Wochenplan sind **keine getrennten Features**, sondern beides Ausprägungen desselben, generischen Zeitplan-Mechanismus über die `config`-Infrastruktur (Phase 11): eine Liste beliebig vieler Zeitplan-Einträge, jeder mit einer eigenen Trigger-Regel + einem referenzierten Programm (Phase 14).

Trigger-Regeln sollen mindestens abdecken:
- **Täglich, feste Uhrzeit** — z. B. „jeden Tag 21:00 Uhr".
- **Wöchentlich, fester Wochentag + Uhrzeit** — z. B. „jeden Dienstag" (+ Uhrzeit).
- **Einmalig, festes Datum + Uhrzeit** — z. B. „genau am 01.02.26, 11:00 Uhr".

Die Liste der Einträge ist beliebig erweiterbar (nicht auf 7 Tage/eine Uhrzeit pro Tag begrenzt) — z. B. mehrere Einträge am selben Tag, unterschiedliche Programme an unterschiedlichen Wochentagen, oder eine Mischung aus wiederkehrenden und einmaligen Terminen gleichzeitig.

Beispiel: Rasen jeden Tag 21:00 Uhr (täglicher Trigger, Programm „Rasen"), Beete jeden Dienstag und Freitag 20:00 Uhr (zwei wöchentliche Trigger, Programm „Beete").

## Voraussetzungen

- Phase 2 (NTP-Sync, Echtzeituhr) ✅ — Datum/Wochentag/Uhrzeit sind bereits verfügbar.
- Phase 7 (Automatik-Sequenz/Sequencer) — der Scheduler triggert im Kern dasselbe wie ein manuelles `main/cmd ON`.
- Phase 11 (`main/config/set`/`state`, JSON-Infrastruktur) — die Zeitplan-Einträge sind strukturell eine weitere Erweiterung desselben `config.json`.
- Phase 14 (Bewässerungsprogramme) — ein Zeitplan-Eintrag wählt ein Programm aus, statt Zeiten/Auto-Flags erneut zu definieren.

## Ganz grob angedacht (nicht final)

- Zeitplan als Array in `config.json`, editierbar über `main/config/set` (Erweiterung von dessen Schema um z. B. `"schedule": [...]`), beliebig viele Einträge.
- Je Eintrag: Trigger-Typ (`daily`/`weekly`/`once` o. ä.), zugehörige Zeit-/Datumsangaben, Programm-Index (Phase 14).
- Einmal pro Minute prüfen, ob „jetzt" einem der konfigurierten Trigger entspricht → zugehöriges Programm anwenden + `main/cmd ON` auslösen (analog zu Phase 14s einmaliger Programmwahl, nur automatisch statt manuell per `main/program/cmd`).

## Offene Fragen (bewusst noch nicht entschieden)

- Exaktes JSON-Schema der Trigger-Regeln (wie werden `daily`/`weekly`/`once` unterschieden, welche Felder je Typ?).
- Verhalten bei verpasstem Trigger (z. B. Reboot/Stromausfall genau im Startfenster) — nachholen oder für diesen Termin ausfallen lassen?
- Was passiert mit einem einmaligen (`once`-)Trigger, nachdem er gefeuert hat — bleibt der Eintrag stehen (feuert nie wieder, da Datum in der Vergangenheit) oder wird er automatisch aus der Liste entfernt?
- Zwei oder mehr Einträge, die zur exakt gleichen Zeit auslösen — nacheinander abarbeiten, oder ist das schlicht Konfigurationsfehler des Nutzers (dokumentieren, nicht technisch verhindern)?
- Eigenes MQTT-Topic zum gezielten Editieren des Zeitplans, oder reicht die generische `main/config/set` dafür aus?
- Verhältnis zu manuellem `main/cmd ON`/`OFF` während eines geplant ausgelösten Laufs — vermutlich identisch zur bestehenden Regel „manuelles Aus wird angenommen, Sequenz macht weiter" (siehe `docs/requirements.md`).

## Zweck des Eintrags

Bewusst früh im Backlog dokumentiert, damit Entscheidungen in den vorherigen Phasen (v. a. Sequencer/Phase 7, Konfiguration/Phase 11, Programme/Phase 14) nicht in eine Richtung laufen, die einen späteren, generischen Scheduler erschwert.
