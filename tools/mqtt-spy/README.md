# mqtt-spy-Konfiguration

[mqtt-spy](https://kamilfb.github.io/mqtt-spy/) ist ein Windows-Java-Tool zum manuellen Beobachten/Publizieren von MQTT-Nachrichten — Ergänzung zu den automatisierten Testskripten in [`tools/mqtt-tests/`](../mqtt-tests/).

## Live-Standort

Die App liest ihre Konfiguration von einem festen Pfad, nicht aus dem Projektverzeichnis:

```
C:\Users\Pfanne\mqtt-spy\mqtt-spy-configuration.xml
```

**`mqtt-spy-configuration.xml` in diesem Ordner ist eine versionierte Kopie davon** — zum Nachlesen/Vergleichen im Repo, nicht die Datei, die die App tatsächlich benutzt. Um eine aktualisierte Version scharf zu schalten, den Inhalt in den Live-Pfad kopieren (die App überschreibt die Live-Datei selbst mit UI-/Verbindungsstatus, deshalb keine Live-Verknüpfung).

## Struktur

- `<Publication topic="...">`: ein Eintrag je setzbarem Topic (`.../set`, `.../cmd`) — das sind die Topics, die man aus mqtt-spy heraus manuell publizieren würde. Reine State-Topics bekommen keinen eigenen Publication-Eintrag.
- `<Subscription topic="..." createTab="true">`: mit `+`-Wildcards sinnvoll zusammengefasst, wo mehrere Ventile/Sub-Bereiche dieselbe Form teilen (z. B. `gartenwasser/+/state` deckt `V0`–`V5`/`state` **und** `main/state` ab, da alle auf derselben Ebene liegen; `gartenwasser/main/+/state` deckt `config/state`, `programs/state`, `program/state`, `schedule/state` ab). Echte Einzeltopics (`main/activeValve`, `main/remainingTotal`, `main/time/maxTime`) bleiben einzeln, da sie keine gemeinsame Wildcard-Ebene mit anderen Topics teilen.

## Aktuell halten

Bei jeder Änderung der MQTT-Topic-Struktur (siehe `docs/requirements.md`, Abschnitt „MQTT-Topic-Struktur", die kanonische Quelle) diese Datei **und** den Live-Pfad aktualisieren. Claude macht das automatisch (siehe Memory-Eintrag „MQTT-Spy Config Sync").
