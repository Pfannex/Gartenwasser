#include "WebManager.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "Logger.h"

namespace {

constexpr uint16_t kHttpPort = 80;
AsyncWebServer server(kHttpPort);

// TODO(Phase 17+): `pio run --target uploadfs` (mklittlefs) schreibt ein LittleFS-Image,
// das der arduino-esp32-3.x-Laufzeit-Mount beim ersten Boot nicht als gueltig erkennt und
// automatisch neu formatiert - die vorab geschriebenen Dateien sind danach weg (auf Hardware
// verifiziert, 2026-08-17). Laufzeit-Schreiben/Lesen ueber LittleFS.open(..., FILE_WRITE)
// funktioniert dagegen nachweislich zuverlaessig (Alias-Persistenz-Test ueberlebt Reboot).
// Deshalb hier als Workaround: die (noch kleinen) Web-Dateien direkt in der Firmware
// eingebettet und beim ersten Boot selbst nach LittleFS geschrieben, statt sich auf
// uploadfs zu verlassen. Inhalt manuell synchron zu data/index.html / data/style.css
// halten. Muss vor Phase 17 (deutlich mehr/groessere Dateien: Alpine.js, mqtt.js) durch
// eine echte Loesung des uploadfs-Formatproblems ersetzt werden - nicht mehr skalierbar.

const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="de">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Gartenwasser</title>
  <link rel="stylesheet" href="style.css" />
</head>
<body>
  <h1>Gartenwasser</h1>
  <p>Web-Interface-Fundament (Phase 16) — statisches File-Serving über LittleFS läuft.</p>

  <div class="card">
    <p>Beispiel-Ventilstatus (Design-Vorschau, noch ohne Live-Daten — folgt in Phase 17):</p>
    <span class="chip" data-state="running">V0</span>
    <span class="chip" data-state="idle">V1</span>
    <span class="chip" data-state="running">V2</span>
    <span class="chip" data-state="auto">V3</span>
    <span class="chip" data-state="idle">V4</span>
    <span class="chip" data-state="auto">V5</span>
  </div>

  <p style="margin-top: 16px;">
    <button class="primary">Beispiel-Button</button>
    <button>Sekundär</button>
  </p>
</body>
</html>
)HTML";

const char kStyleCss[] PROGMEM = R"CSS(/* Gartenwasser Web-Interface - "Dashboard Cards" Design-Tokens (Phase 16)
 * Basis fuer alle Folgephasen (17-20). Farblogik deckungsgleich mit der
 * Touch-UI-Ventilmatrix (gruen=auto/idle, dunkelgrau=aus, rot=laeuft).
 */

:root {
  --bg: #F3F5F4;
  --surface: #FFFFFF;
  --surface-sunken: #EDF1F0;
  --text: #16211E;
  --text-muted: #5B6B67;
  --border: #DCE3E1;
  --accent: #1C6E8C;
  --accent-ink: #FFFFFF;
  --state-running: #C9463A;
  --state-auto: #3C9B5D;
  --state-idle: #8B958F;
  --radius: 14px;
  --shadow: 0 1px 2px rgba(20, 30, 28, 0.06), 0 6px 16px rgba(20, 30, 28, 0.05);
}

@media (prefers-color-scheme: dark) {
  :root {
    --bg: #10161A;
    --surface: #171F23;
    --surface-sunken: #10161A;
    --text: #E7EEEC;
    --text-muted: #93A29D;
    --border: #2A3438;
    --accent: #6FC6E8;
    --accent-ink: #08222B;
    --state-running: #E27168;
    --state-auto: #63C58A;
    --state-idle: #6E7A76;
    --shadow: 0 1px 2px rgba(0, 0, 0, 0.3), 0 6px 20px rgba(0, 0, 0, 0.35);
  }
}

* { box-sizing: border-box; }

body {
  margin: 0;
  background: var(--bg);
  color: var(--text);
  font-family: ui-sans-serif, -apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  line-height: 1.5;
  padding: 24px 16px 48px;
}

h1, h2, h3 {
  font-weight: 650;
  letter-spacing: -0.01em;
  text-wrap: balance;
  margin: 0 0 12px;
}

p { margin: 0 0 8px; color: var(--text-muted); }

.card {
  background: var(--surface-sunken);
  border-radius: var(--radius);
  padding: 14px 16px;
  box-shadow: var(--shadow);
}

.chip {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border-radius: 10px;
  font-size: 12px;
  font-weight: 700;
  color: #fff;
  padding: 4px 10px;
}
.chip[data-state="running"] { background: var(--state-running); }
.chip[data-state="auto"] { background: var(--state-auto); }
.chip[data-state="idle"] { background: var(--state-idle); }

button {
  font-family: inherit;
  font-size: 13.5px;
  font-weight: 600;
  padding: 9px 16px;
  border-radius: 10px;
  cursor: pointer;
  border: none;
  background: var(--surface-sunken);
  color: var(--text);
}
button.primary { background: var(--accent); color: var(--accent-ink); }
)CSS";

// Schreibt `content` nach `path`, aber nur wenn die Datei noch nicht existiert (idempotent -
// kein Rewrite bei jedem Boot).
void writeFileIfMissing(const char *path, const char *content) {
  if (LittleFS.exists(path)) {
    return;
  }
  File file = LittleFS.open(path, FILE_WRITE);
  if (!file) {
    Logger::logf(Logger::Type::ERROR, Logger::Source::WEB, "WebManager: %s nicht schreibbar.", path);
    return;
  }
  file.print(content);
  file.close();
  Logger::logf(Logger::Type::INFO, Logger::Source::WEB, "WebManager: %s angelegt.", path);
}

}  // namespace

void WebManager::begin() {
  writeFileIfMissing("/index.html", kIndexHtml);
  writeFileIfMissing("/style.css", kStyleCss);

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); });
  server.begin();
  Logger::log(Logger::Type::INFO, Logger::Source::WEB, "WebManager: Webserver gestartet (Port 80).");
}
