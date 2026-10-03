# Home-Assistant-Integration

Die Home-Assistant-Seite der Gartenbewässerung (Packages mit Hilfswerten, Skripten, Templates und
Automationen, das Dashboard sowie das openHASP-Touchpanel `plate_wz`) liegt seit 2026-10-03 im Repo
[HomeAssistant](https://github.com/Pfannex/HomeAssistant):

- Konfiguration: `configurations/gartenwasser/`, `configurations/packages/gartenwasser.yaml`
- Dashboard: `dashboards/handy/gartenwasser/`
- openHASP-Panel: `configurations/plates/` (inkl. Sicherung der Display-Dateien unter `plate_wz/device/`)
- Erklärung, Einrichtung und Lessons Learned (Reload-Verhalten, Ghost-State, Freeze-Properties u. a.):
  `docs/gartenwasser_ha.md` (der bisherige Inhalt dieser Datei)

Schnittstelle zwischen Gerät und Home Assistant bleibt die MQTT-Topic-Struktur in `docs/requirements.md`.
Der Stand vor dem Umzug ist in der Git-Historie dieses Repos erhalten.
