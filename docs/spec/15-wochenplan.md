# Phase 15 — Wochenplan (Scheduler)

**Status:** 📋 Backlog, grob skizziert — Details bei Umsetzung klären

## Ziel

Für jeden Wochentag automatisch ein Bewässerungsprogramm (Phase 14) anwenden und die Automatik-Sequenz (Phase 7) autonom zu einer konfigurierten Uhrzeit auslösen — ohne manuelles `main/cmd ON`.

Beispiel: Rasen jeden Tag, Beete nur alle 2 Tage → an geraden Wochentagen ein Programm mit `auto` nur für die Rasen-Ventile, an ungeraden eines mit `auto` für Rasen **und** Beete.

## Voraussetzungen

- Phase 2 (NTP-Sync, Echtzeituhr) ✅ — Wochentag/Uhrzeit sind bereits verfügbar.
- Phase 7 (Automatik-Sequenz/Sequencer) — der Scheduler triggert im Kern dasselbe wie ein manuelles `main/cmd ON`.
- Phase 14 (Bewässerungsprogramme) — ein Wochenplan-Eintrag wählt ein Programm aus, statt Zeiten/Auto-Flags erneut zu definieren.

## Ganz grob angedacht (nicht final)

- 7 Einträge (Mo–So), je Eintrag: Programm-Index (Phase 14) + Startzeit.
- Einmal pro Minute prüfen, ob „jetzt" der konfigurierten Startzeit des heutigen Wochentags entspricht → Programm anwenden + `main/cmd ON` auslösen.

## Offene Fragen (bewusst noch nicht entschieden)

- Startzeit global für alle Wochentage oder individuell pro Tag?
- Verhalten bei verpasstem Trigger (z. B. Reboot/Stromausfall genau im Startfenster) — nachholen oder für diesen Tag ausfallen lassen?
- Ein Programm pro Tag, oder mehrere Startzeiten/Programme an einem Tag möglich?
- Eigenes MQTT-Topic zum Editieren des Wochenplans (`main/schedule/set`, JSON) — vermutlich analog zu `main/config/set` (Phase 11), da strukturell ähnlich (Array editierbar, unbekannte Werte ignorieren).
- Verhältnis zu manuellem `main/cmd ON`/`OFF` während eines geplanten Laufs — vermutlich identisch zur bestehenden Regel „manuelles Aus wird angenommen, Sequenz macht weiter" (siehe `docs/requirements.md`).

## Zweck des Eintrags

Bewusst früh im Backlog dokumentiert, damit Entscheidungen in den vorherigen Phasen (v. a. Sequencer/Phase 7, Programme/Phase 14) nicht in eine Richtung laufen, die einen späteren Scheduler erschwert.
