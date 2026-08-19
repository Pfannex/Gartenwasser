// Gartenwasser Web-Interface - Live-Log (Nachtrag 2026-08-18)
// Architektur B (siehe docs/spec/16-webif-fundament.md): eigene MQTT-over-WebSocket-
// Verbindung wie die anderen Seiten, aber schlank - abonniert bewusst nur die zwei Topics,
// die diese Seite tatsaechlich braucht (kein "gartenwasser/#" wie beim Dashboard).

const BROKER_WS_URL = "ws://192.168.1.123:9001/mqtt";
const TOPIC_PREFIX = "gartenwasser/";
const MAX_LOG_LINES = 500; // eigene Seite, mehr Platz/Kontext als die vorherige Dashboard-Karte

// PUB/SUB kommen seit 2026-08-18 grundsaetzlich durch (siehe MqttManager::onLoggerLine()) -
// nur main/remainingTotal und V{n}/time/remaining werden dort gezielt herausgefiltert
// (Sekundentakt waehrend der Automatik, reines Rauschen ohne Zusatzinfo).
const LOG_TYPES = ["ERROR", "INFO", "DEBUG", "PUB", "SUB"];
const LOG_SOURCES = ["WIFI", "MQTT", "I2C", "HMI", "WEB", "SYS", "VALVE", "SEQ", "OTA"];

function livelog() {
  return {
    connected: false,
    deviceOnline: false,
    logLines: [],
    logIdCounter: 0,
    logTypes: LOG_TYPES,
    logSources: LOG_SOURCES,
    // Leer = keine Einschraenkung (alles sichtbar) - ausgewaehlte Chips schraenken auf genau
    // diese Werte ein (Facetten-Filter, nicht Ausschluss-Toggle: "nichts gedrueckt" zeigt
    // alles, "einen Typ gedrueckt" zeigt nur noch diesen).
    activeTypes: {},
    activeSources: {},
    searchText: "",
    sourceMenuOpen: false,
    typeMenuOpen: false,
    eventSearchActive: false,
    footerVersion: "",

    init() {
      this.client = mqtt.connect(BROKER_WS_URL);
      this.client.on("connect", () => {
        this.connected = true;
        this.client.subscribe(TOPIC_PREFIX + "diagnostics/livelog");
        this.client.subscribe(TOPIC_PREFIX + "diagnostics/version");
        this.client.subscribe(TOPIC_PREFIX + "availability");
        // Kein Retain auf diagnostics/livelog (reiner Stream) - ohne diese Anfrage saehe eine
        // frisch geoeffnete Log-Seite nur kuenftige Zeilen, nichts von vorher.
        this.client.publish(TOPIC_PREFIX + "diagnostics/livelog/replay", "1");
      });
      this.client.on("close", () => {
        this.connected = false;
      });
      this.client.on("message", (topic, payload) => this.handleMessage(topic.slice(TOPIC_PREFIX.length), payload.toString()));
    },

    handleMessage(topic, payload) {
      if (topic === "diagnostics/livelog") {
        this.appendLogLine(payload);
      } else if (topic === "availability") {
        this.deviceOnline = payload === "online";
      } else if (topic === "diagnostics/version") {
        this.footerVersion = payload;
      }
    },

    // "hh:mm:ss:mmm CLASS TYPE message" (siehe Logger::log()) - CLASS/TYPE sind auf 5 Zeichen
    // leerzeichenaufgefuellt (z.B. "I2C  ", "INFO "), zwischen den Feldern stehen dadurch
    // unterschiedlich viele Leerzeichen (Fuellzeichen + Trennzeichen) - \s+ statt eines
    // einzelnen Leerzeichens noetig, sonst schlaegt das Parsen bei den meisten Zeilen fehl.
    appendLogLine(raw) {
      const m = raw.match(/^(\S+)\s+(\S+)\s+(\S+)\s+(.*)$/);
      const message = m ? m[4] : raw;
      const json = this.parseJsonMessage(message);
      this.logLines.push({
        id: this.logIdCounter++,
        raw,
        time: m ? m[1] : "",
        source: m ? m[2].trim() : "",
        type: m ? m[3].trim() : "",
        message,
        jsonTopic: json ? json.topic : null,
        jsonPretty: json ? json.pretty : null,
        jsonTruncated: json ? json.truncated : false,
        // Standardmaessig eingeklappt (2026-08-19) - Nutzer-Feedback: der Pretty-Print-Kasten
        // blaeht sonst jede JSON-Zeile sofort auf, auch wenn man den Inhalt gar nicht braucht.
        jsonExpanded: false,
      });
      if (this.logLines.length > MAX_LOG_LINES) this.logLines.shift();
      this.$nextTick(() => {
        const el = this.$refs.logView;
        if (el) el.scrollTop = el.scrollHeight;
      });
    },

    // PUB/SUB-Zeilen haben die Form "topic = payload" (siehe MqttManager::publishAndLog()/
    // handleMqttMessage()). Wenn der Payload-Teil wie JSON aussieht ('{'/'[' als erstes
    // Zeichen), wird er im Code-Kasten dargestellt statt als Einzeiler - bei gueltigem JSON
    // eingerueckt (pretty), bei ungueltigem (z.B. abgeschnittenem main/programs/state/
    // schedule/state - potenziell mehrere KB gross, passen auch mit dem vergroesserten
    // Logger-Puffer nicht immer komplett rein) als Rohtext mit "(gekuerzt)"-Hinweis - sieht
    // sonst wie eine kaputte Darstellung aus statt einer bewussten Grenze (Nutzer-Feedback
    // 2026-08-18: "klappt nur teilweise", der reine Fliesstext-Fallback wirkte kaputt).
    parseJsonMessage(message) {
      const sep = message.indexOf(" = ");
      if (sep === -1) return null;
      const topic = message.slice(0, sep);
      const value = message.slice(sep + 3).trim();
      if (value[0] !== "{" && value[0] !== "[") return null;
      try {
        return { topic, pretty: JSON.stringify(JSON.parse(value), null, 2), truncated: false };
      } catch (e) {
        return { topic, pretty: value, truncated: true };
      }
    },

    toggleType(type) {
      if (this.activeTypes[type]) delete this.activeTypes[type];
      else this.activeTypes[type] = true;
    },

    toggleSource(source) {
      if (this.activeSources[source]) delete this.activeSources[source];
      else this.activeSources[source] = true;
    },

    toggleSourceMenu() {
      this.sourceMenuOpen = !this.sourceMenuOpen;
      this.typeMenuOpen = false;
    },

    toggleTypeMenu() {
      this.typeMenuOpen = !this.typeMenuOpen;
      this.sourceMenuOpen = false;
    },

    // Event-Spaltenkopf wird bei Klick zum Sucheingabefeld (ersetzt die frueher separate
    // Suchleiste ueber der Tabelle). Beim Verlassen des Feldes (Blur) klappt es wieder auf
    // eine Beschriftung zu - "Event", falls kein Suchbegriff aktiv ist, sonst "Eventfilter:
    // <Begriff>" (weiterhin anklickbar, um den Filter zu bearbeiten).
    activateEventSearch() {
      this.eventSearchActive = true;
      this.$nextTick(() => {
        if (this.$refs.eventSearchInput) this.$refs.eventSearchInput.focus();
      });
    },

    clearEventSearch() {
      this.searchText = "";
      this.eventSearchActive = false;
    },

    // Spaltenkopf-Filter (Quelle/Typ, Mehrfachauswahl per Checkbox-Liste, Facetten-Prinzip:
    // leere Auswahl = keine Einschraenkung) + Freitextsuche (Volltext auf die Rohzeile)
    // kombiniert - unabhaengig voneinander, das Suchfeld bleibt beim Filtern unveraendert.
    // Jeder Tastendruck im Suchfeld wertet das ueber Alpines Reaktivitaet automatisch neu aus.
    visibleLines() {
      const query = this.searchText.trim().toLowerCase();
      const typeFilterActive = Object.keys(this.activeTypes).length > 0;
      const sourceFilterActive = Object.keys(this.activeSources).length > 0;
      return this.logLines.filter((l) => {
        if (typeFilterActive && !this.activeTypes[l.type]) return false;
        if (sourceFilterActive && !this.activeSources[l.source]) return false;
        if (query && !l.raw.toLowerCase().includes(query)) return false;
        return true;
      });
    },
  };
}
