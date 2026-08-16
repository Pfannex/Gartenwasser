# Phase 15 — Zeitplan / Scheduler (Tages- und Wochenplan)

**Status:** 📋 Design-Entwurf abgestimmt (2026-08-16), Umsetzung offen

## Ziel

Automatisch und autonom Bewässerungsprogramme (Phase 14) zu konfigurierten Zeitpunkten auslösen (`main/cmd ON`-Äquivalent) — ohne manuellen Eingriff. Tages- und Wochenplan sind **keine getrennten Features**, sondern beides Ausprägungen desselben, generischen Zeitplan-Mechanismus in einem eigenen `schedule`-Konfigurationsbereich (eigene Datei `/schedule.json`, eigenes Topic-Paar `main/schedule/set`/`main/schedule/state` — siehe `docs/requirements.md`, Abschnitt „Konfiguration“, Entscheidung 2026-08-16): eine Liste beliebig vieler Zeitplan-Einträge, jeder mit einer eigenen Trigger-Regel + einem referenzierten Programm (Phase 14).

Trigger-Regeln sollen mindestens abdecken:
- **Täglich, feste Uhrzeit** — z. B. „jeden Tag 21:00 Uhr".
- **Wöchentlich, fester Wochentag + Uhrzeit** — z. B. „jeden Dienstag" (+ Uhrzeit).
- **Einmalig, festes Datum + Uhrzeit** — z. B. „genau am 01.02.26, 11:00 Uhr".

Die Liste der Einträge ist beliebig erweiterbar (nicht auf 7 Tage/eine Uhrzeit pro Tag begrenzt) — z. B. mehrere Einträge am selben Tag, unterschiedliche Programme an unterschiedlichen Wochentagen, oder eine Mischung aus wiederkehrenden und einmaligen Terminen gleichzeitig.

Beispiel: Rasen jeden Tag 21:00 Uhr (täglicher Trigger, Programm „Rasen"), Beete jeden Dienstag und Freitag 20:00 Uhr (zwei wöchentliche Trigger, Programm „Beete").

## Voraussetzungen

- Phase 2 (NTP-Sync, Echtzeituhr) ✅ — Datum/Wochentag/Uhrzeit sind bereits verfügbar.
- Phase 7 (Automatik-Sequenz/Sequencer) — der Scheduler triggert im Kern dasselbe wie ein manuelles `main/cmd ON`.
- Phase 11 (`main/config/set`/`state`, JSON-Infrastruktur) — `schedule` folgt demselben Bulk-Set/State-Muster (eigene Datei/Topics statt Erweiterung von `config.json`, siehe Entscheidung 2026-08-16).
- Phase 14 (Bewässerungsprogramme) — ein Zeitplan-Eintrag wählt ein Programm aus, statt Zeiten/Auto-Flags erneut zu definieren.

## Design (erster Aufschlag, 2026-08-16 — noch nicht final, Details bei Umsetzung pruefen)

### JSON-Schema

```json
{
  "enabled": true,
  "schedule": [
    {"name": "Rasen abends", "enabled": true, "type": "daily", "time": "21:00", "program": "Rasen"},
    {"name": "Beete Di/Fr",  "enabled": true, "type": "weekly", "weekdays": ["tue", "fri"], "time": "20:00", "program": "Beete"},
    {"name": "Fruehjahrsstart", "enabled": true, "type": "once", "date": "2026-02-01", "time": "11:00", "program": "Kurz"}
  ]
}
```

- **`type`** unterscheidet `daily`/`weekly`/`once`; typ-spezifische Felder (`weekdays` nur bei `weekly`, `date` nur bei `once`).
- **`program`** referenziert ein Bewässerungsprogramm **per Name, nicht per Array-Index** (Entscheidung 2026-08-16) — sonst würde ein Umsortieren der Programme via `main/programs/set` die Zeitplan-Referenzen stillschweigend auf ein anderes Programm verschieben (dasselbe Problem, das die `shortcut`-Felder für `P1`–`P4` bereits lösen). Verhalten bei nicht mehr existierendem Namen (Programm umbenannt/gelöscht) noch zu klären — vermutlich analog zu ungültigem `main/program/cmd`-Index: ignorieren + loggen.
- **`enabled`** je Eintrag: einzelne Einträge pausieren, ohne sie zu löschen.
- **`enabled`** auf oberster Ebene (2026-08-16 bestätigt): globaler Ein/Aus-Schalter für den kompletten Zeitplan — bei `false` ist der Zeitplan komplett außer Betrieb (kein Eintrag löst aus), ohne dass die Konfiguration verloren geht (z. B. „Urlaubsmodus“). Zusätzlich zum Bulk-Feld ein schlankes Convenience-Topic `main/schedule/cmd` (`ON`/`OFF`), analog zu `main/program/cmd` als Singular-Pendant zu `main/programs/set` — praktisch für ein späteres Home-Assistant-Switch-Entity (Phase 10), ohne JSON senden zu müssen.
- Einmal pro Minute prüfen, ob „jetzt" einem aktiven (`enabled`) Trigger entspricht → referenziertes Programm auswählen + Sequenz starten (derselbe Pfad wie `main/cmd ON`, inkl. der seit Phase 14 geltenden Regel „Automatik erfordert Programm“ — der Scheduler hat ja durch die Referenz immer eins).

### Entschiedene Punkte

1. **Programm-Referenz per Name** (siehe oben) statt Array-Index.
2. **Verpasster Trigger (Reboot/Stromausfall im Startfenster) → verfällt, wird nicht nachgeholt.** Begründung (Nutzer, 2026-08-16): sonst würde automatisch etwas zu einem Zeitpunkt passieren, den man nicht explizit gewünscht hat — ein nachgeholter Trigger zu einer unvorhersehbaren Zeit (z. B. erst beim nächsten Boot Stunden später) wäre überraschender/unerwünschter als ein einmalig ausgefallener Termin.
3. **Globaler Ein/Aus-Schalter** (`enabled` + `main/schedule/cmd`) — siehe oben.

### Neue Punkte (Nutzer-Merker, 2026-08-16, noch zu verfeinern)

- **Kollisions-Hinweis bei manuellem `main/cmd ON`** (Touch **und** MQTT): läuft eine manuell gestartete Sequenz voraussichtlich noch, wenn der nächste Zeitplan-Trigger fällig wird (`main/remainingTotal` würde über den nächsten geplanten Start hinausreichen), soll ein Hinweis erfolgen — nicht blockierend, nur Information (passt zum bisherigen Stil: Hinweis statt Verhinderung, siehe `docs/spec/13-touch-ui.md`). Setzt voraus, dass der Scheduler jederzeit den „nächsten fälligen Trigger“ berechnen kann. Anzeige vermutlich Touch-Statuszeile (transienter Hinweis) + `lastError`/Log für den MQTT-Fall — Details bei Umsetzung.
- **Aufräum-Funktion für abgelaufene `once`-Einträge**: ein Topic (Arbeitstitel `main/schedule/cleanup`, genaue Benennung noch offen) entfernt alle Einträge, die nie wieder auslösen können (abgelaufene `once`-Termine). Kein Automatismus — bewusst nur auf Anfrage, damit „warum ist mein Eintrag weg“ nicht überrascht. Ergänzt (löst aber nicht ab) das oben entschiedene „liegen lassen“-Verhalten: abgelaufene Einträge bleiben standardmäßig informativ stehen, können aber bei Bedarf gezielt aufgeräumt werden.

### Noch offene Fragen

- Exaktes Verhalten, wenn ein `program`-Name in einem Zeitplan-Eintrag auf kein existierendes Programm mehr zeigt.
- Zwei oder mehr Einträge, die zur exakt gleichen Zeit auslösen — nacheinander abarbeiten, oder ist das schlicht Konfigurationsfehler des Nutzers (dokumentieren, nicht technisch verhindern)?
- Verhältnis zu manuellem `main/cmd ON`/`OFF` während eines geplant ausgelösten Laufs — vermutlich identisch zur bestehenden Regel „manuelles Aus wird angenommen, Sequenz macht weiter" (siehe `docs/requirements.md`), plus der neue Kollisions-Hinweis oben.
- Exakte Topic-Benennung für die Cleanup-Funktion.

## Zweck des Eintrags

Bewusst früh im Backlog dokumentiert, damit Entscheidungen in den vorherigen Phasen (v. a. Sequencer/Phase 7, Konfiguration/Phase 11, Programme/Phase 14) nicht in eine Richtung laufen, die einen späteren, generischen Scheduler erschwert.
