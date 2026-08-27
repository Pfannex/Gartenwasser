# Gartenwasser — Entwickler-Dokumentation

Ergänzt `docs/requirements.md` (funktionale Spezifikation) und `docs/manual/usermanual.md`
(Endnutzer-Handbuch) um Werkzeuge/Vorgehen für die Arbeit am Quelltext selbst — hier landet
alles, was beim Verstehen/Navigieren der Codebasis hilft, aber weder Spezifikation noch
Bedienungsanleitung ist.

Dieses Dokument deckt den **C++-Firmware-Teil** ab (Doxygen). Für den **nicht-C-Teil** —
Datenfluss/Abhängigkeiten zwischen Gerät, MQTT, Web-Interface, Home Assistant und dem
openHASP-Touchpanel — siehe [`docs/architecture.md`](architecture.md).

## Code-Struktur visualisieren (Doxygen + Graphviz)

Die Firmware besteht aus 10 Klassen + 3 Namespaces in `src/` (`AutomaticController`,
`Diagnostics`, `FileSystem`, `HMI`, `I2C`, `Logger`, `OTA`, `ValveController`, `ValveTimer`,
`WebIF` als Klassen; `HaDiscovery`, `MQTT`, `WiFiController` als Namespaces) — bei wachsender
Codebasis wird "wer ruft wen auf" und "wer hängt von wem ab" per reinem Lesen mühsam.
[Doxygen](https://www.doxygen.nl/) + [Graphviz](https://graphviz.org/) erzeugen dafür
automatisch Klassendiagramme, Aufruf-/Aufrufer-Graphen (call/caller graphs) und
Include-Abhängigkeitsgraphen direkt aus dem Quelltext — ganz ohne eigene Doc-Kommentare
(`EXTRACT_ALL` ist aktiviert, da der Code aktuell kaum Doxygen-Kommentare enthält).

### Installation (einmalig)

```
winget install DimitriVanHeesch.Doxygen
winget install Graphviz.Graphviz
```

Falls `doxygen`/`dot` danach im Terminal nicht gefunden werden: neues Terminal öffnen (PATH
wird von winget in die Registry geschrieben, ein bereits laufendes Terminal hat davon noch
nichts mitbekommen).

### Konfiguration

Liegt versioniert unter [`tools/doxygen/Doxyfile`](../tools/doxygen/Doxyfile) — generiert per
`doxygen -g` und für dieses Projekt angepasst:

| Einstellung | Wert | Warum |
|---|---|---|
| `INPUT` | `src include` | Nur eigener Code, nicht `.pio`/Bibliotheken |
| `EXCLUDE` | `include/secrets.h`, `include/lv_conf.h` | Zugangsdaten bzw. reine Bibliothekskonfiguration ohne eigenen Code |
| `EXTRACT_ALL` | `YES` | Dokumentiert auch unkommentierte Klassen/Funktionen (Code hat kaum Doxygen-Kommentare) |
| `HAVE_DOT`, `CALL_GRAPH`, `CALLER_GRAPH`, `CLASS_GRAPH` | `YES` | Aufruf-/Klassen-/Kollaborationsgraphen per Graphviz |
| `DOT_IMAGE_FORMAT` | `svg` (interaktiv) | Zoombar, in Browser navigierbar (Klick auf Knoten springt zur jeweiligen Seite) |
| `GENERATE_XML` | `YES` | Maschinenlesbare Struktur, siehe unten |
| `GENERATE_LATEX` | `NO` | Nicht benötigt, spart Generierungszeit |

### Regenerieren

```
cd tools/doxygen
doxygen Doxyfile
```

Ausgabe landet in [`docs/doxygen/`](doxygen/html/index.html) (`html/` + `xml/`) — **versioniert
mitgeliefert** (bewusste Entscheidung, siehe unten), damit die Code-Doku direkt im Repository
verfügbar ist, ohne dass Doxygen lokal installiert sein muss, nur um sie anzusehen. Nach
Code-Änderungen neu laufen lassen und die entstandenen Änderungen unter `docs/doxygen/`
committen — kein Auto-Sync, die Ausgabe kann also veralten, wenn das Neu-Generieren vergessen
wird.

### Ansehen

**→ [Code-Struktur durchsuchen](doxygen/html/index.html)** — direkter Einstieg in die
generierte Doku. Weitere Einstiegspunkte dort: *Classes* (Klassenliste mit
Kollaborationsdiagramm je Klasse), *Files* (Include-Graph je Datei), oder direkt auf eine
Methode klicken → Call-Graph (was ruft diese Methode auf) bzw. Caller-Graph (wer ruft sie auf)
am Seitenende.

### Für Claude nutzbar

Die XML-Ausgabe (`docs/doxygen/xml/`) ist strukturiert und maschinell auswertbar (eine Datei
je Klasse/Namespace, u. a. Member-Listen, Parameter, Aufruf-Referenzen zwischen Funktionen).
Bei Fragen zur Codestruktur ("was ruft `HaDiscovery::publishAll` auf", "welche Klassen hängen
von `MQTT` ab") kann dort gezielt nachgesehen werden, statt den kompletten Quelltext zu
durchsuchen — Voraussetzung ist ein aktueller Lauf (siehe oben).

### Warum versioniert statt gitignored

Ursprünglich als reines Build-Artefakt gitignored — auf Nutzerwunsch umgestellt, damit die
Code-Struktur-Doku direkt im Repository/auf GitHub einsehbar ist (z. B. `docs/doxygen/html/
index.html` lokal im Browser öffnen, ganz ohne Doxygen-Installation). Kehrseite: 926 Dateien,
~6,3 MB, die bei jeder Regenerierung als Diff auftauchen und manuell committed werden müssen.

### Bekannte Kleinigkeit

Jede `.cpp`-Datei mit einem anonymen Namespace für dateiinterne Hilfsfunktionen (übliches
C++-Idiom für internal linkage) taucht in der generierten Namespace-Liste als kryptischer,
automatisch vergebener Name auf (`@0301...`) — harmlos, einfach ignorieren.
