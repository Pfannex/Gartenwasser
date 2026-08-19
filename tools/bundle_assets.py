"""PlatformIO Pre-Build-Script: fasst pro WebIF-Seite Seiten-JS + alpine.min.js zu einer
einzigen data/<seite>.bundle.js zusammen (Nachtrag 2026-08-19). mqtt.min.js bleibt bewusst
EIGENSTAENDIG (siehe unten, Speicherplatzgrund) und wird weiterhin direkt aus data/ ausgeliefert.

Hintergrund: das ESP-IDF-Framework ist fest auf CONFIG_LWIP_MAX_ACTIVE_TCP=16 gleichzeitige
TCP-Verbindungen kompiliert (Arduino-Framework liefert lwip vorkompiliert, laesst sich ohne
kompletten ESP-IDF-Rebuild nicht anheben). Ein einzelner Seitenaufruf lud bisher 5 Dateien
(HTML, CSS, mqtt.min.js, <seite>.js, alpine.min.js) - mobile WebKit-Browser (Safari-Engine,
auch "Chrome" auf iOS) oeffnen dafuer oft mehrere parallele Verbindungen statt sie nacheinander
zu laden. Kombiniert mit einer schwachen/instabilen WLAN-Verbindung eines Clients (haengende
Verbindungen belegen TCP-Plaetze laenger) reichte das, um die 16er-Grenze zu sprengen -
Symptom: das WebIF wird fuer ALLE Clients unerreichbar, bis das Geraet neu startet.

mqtt.min.js NICHT mitbuendeln: es waere sonst in allen sechs MQTT-Seiten-Bundles dupliziert
(369 KB x 6 = ueber 2,2 MB allein dafuer) - passt nicht in die ca. 1,79-MB-LittleFS-Partition
("webfs", siehe partitions.csv). Nur <seite>.js + alpine.min.js zusammenzufassen reduziert
die Anfragen pro Seitenaufruf trotzdem von 5 auf 4 (HTML, CSS, mqtt.min.js, Bundle) und bleibt
dabei deutlich unter der Kapazitaetsgrenze.

Beide Dateien definieren nur eine Top-Level-Factory-Funktion (kein synchroner Code, der sofort
beim Laden ausgefuehrt wird - siehe z.B. `function dashboard() { return {...} }` in app.js) -
deshalb ist es unschaedlich, sie zusammenzufassen und mit `defer` zu laden: mqtt.min.js laedt
weiterhin synchron VOR dem Bundle (definiert die globale `mqtt`-Variable), die tatsaechliche
mqtt.connect()-Verbindung passiert erst in init(), wenn Alpine.js die Komponente nach
DOMContentLoaded mountet - die Reihenfolge bleibt also korrekt.

Erzeugte data/*.bundle.js-Dateien sind reine Build-Artefakte (siehe .gitignore). Die
einzelnen Quelldateien (mqtt.min.js/alpine.min.js/<seite>.js) liegen bewusst NICHT in data/,
sondern in web-src/ - mqtt.min.js wird von hier aus 1:1 nach data/ kopiert (weiterhin als
eigene Datei, nicht gebuendelt), alpine.min.js/<seite>.js nur noch gebuendelt.
"""

import os
import shutil

Import("env")  # noqa: F821 (von PlatformIO zur Laufzeit injiziert)

project_dir = env.get("PROJECT_DIR")
data_dir = os.path.join(project_dir, "data")
src_dir = os.path.join(project_dir, "web-src")

# (Seiten-JS, Bundle-Name, MQTT benoetigt?) - ota.js hat bewusst keine MQTT-Verbindung
# (siehe docs/spec/21-webif-ota.md), alle anderen Seiten schon.
pages = [
    ("app.js", "status.bundle.js", True),
    ("konfig.js", "konfiguration.bundle.js", True),
    ("programme.js", "programme.bundle.js", True),
    ("zeitplan.js", "zeitplan.bundle.js", True),
    ("log.js", "log.bundle.js", True),
    ("info.js", "info.bundle.js", True),
    ("ota.js", "ota.bundle.js", False),
]


def read(name):
    with open(os.path.join(src_dir, name), encoding="utf-8") as f:
        return f.read()


alpine_src = read("alpine.min.js")

for page_js, bundle_name, uses_mqtt in pages:
    bundle = read(page_js) + "\n;\n" + alpine_src  # ";" als Trenner gegen ASI-Fallstricke
    with open(os.path.join(data_dir, bundle_name), "w", encoding="utf-8") as f:
        f.write(bundle)

# mqtt.min.js bleibt unveraendert eigenstaendig, nur der Ablageort wechselt (web-src/ -> data/).
shutil.copyfile(os.path.join(src_dir, "mqtt.min.js"), os.path.join(data_dir, "mqtt.min.js"))

print(f"Asset-Bundles erzeugt: {len(pages)} Seiten (data/*.bundle.js) + mqtt.min.js kopiert")
