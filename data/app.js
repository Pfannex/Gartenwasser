// Gartenwasser Web-Interface - Status-Dashboard (Phase 17, Hauptseiten-Redesign)
// Architektur B (siehe docs/spec/16-webif-fundament.md): der Browser verbindet sich
// direkt per MQTT-over-WebSocket mit dem Broker, nutzt exakt dieselben Topics wie
// mqtt-spy/Home Assistant - der ESP32 liefert nur diese statischen Dateien aus.

// Broker-Adresse fest hinterlegt (wie in tools/mqtt-tests/*.py) - anders als die
// Geraete-IP liegt der Broker in einem eigenen Netzsegment, kann nicht automatisch
// aus location.hostname abgeleitet werden.
const BROKER_WS_URL = "ws://192.168.1.123:9001/mqtt";
const TOPIC_PREFIX = "gartenwasser/";

// "mm:ss" -> Sekunden, fuer die Fortschrittsberechnung im Hero-Bereich.
function parseMmSs(str) {
  const parts = (str || "00:00").split(":").map(Number);
  return (parts[0] || 0) * 60 + (parts[1] || 0);
}

function dashboard() {
  return {
    connected: false,
    deviceOnline: false,
    valves: [0, 1, 2, 3, 4, 5].map((i) => ({ index: i, on: false, auto: false, alias: "", remaining: "00:00" })),
    sequenceRunning: false,
    activeValve: "-",
    remainingTotal: "00:00",
    sequenceTotalSeconds: null, // beim Sequenzstart erfasst, Basis fuer die Gesamt-Fortschrittsanzeige
    activeValveTotalSeconds: null, // beim Ventilwechsel erfasst, Basis fuer die Ventil-Fortschrittsanzeige
    activeProgramName: null,
    programs: [],
    activeProgramIndex: 0,
    i2cStatus: "ok",
    lastError: "",

    init() {
      this.client = mqtt.connect(BROKER_WS_URL);
      this.client.on("connect", () => {
        this.connected = true;
        this.client.subscribe(TOPIC_PREFIX + "#");
      });
      this.client.on("close", () => {
        this.connected = false;
      });
      this.client.on("message", (topic, payload) => this.handleMessage(topic.slice(TOPIC_PREFIX.length), payload.toString()));
    },

    handleMessage(topic, payload) {
      const valveMatch = topic.match(/^V(\d)\/(state|auto\/state|alias|time\/remaining)$/);
      if (valveMatch) {
        const valve = this.valves[parseInt(valveMatch[1], 10)];
        if (valveMatch[2] === "state") valve.on = payload === "ON";
        else if (valveMatch[2] === "auto/state") valve.auto = payload === "ON";
        else if (valveMatch[2] === "alias") valve.alias = payload;
        else if (valveMatch[2] === "time/remaining") {
          valve.remaining = payload;
          if (this.sequenceRunning && this.activeValve === "V" + valveMatch[1] && this.activeValveTotalSeconds === null) {
            this.activeValveTotalSeconds = parseMmSs(payload);
          }
        }
        return;
      }
      switch (topic) {
        case "availability":
          this.deviceOnline = payload === "online";
          break;
        case "main/state": {
          const wasRunning = this.sequenceRunning;
          this.sequenceRunning = payload === "ON";
          if (!this.sequenceRunning || (this.sequenceRunning && !wasRunning)) {
            this.sequenceTotalSeconds = null; // neu erfassen: beim naechsten remainingTotal
          }
          break;
        }
        case "main/activeValve":
          if (this.activeValve !== payload) this.activeValveTotalSeconds = null; // neues Ventil: neu erfassen
          this.activeValve = payload;
          break;
        case "main/remainingTotal":
          this.remainingTotal = payload;
          if (this.sequenceRunning && this.sequenceTotalSeconds === null) {
            this.sequenceTotalSeconds = parseMmSs(payload);
          }
          break;
        case "main/program/state":
          try {
            this.activeProgramName = JSON.parse(payload).name;
          } catch (e) {
            this.activeProgramName = null;
          }
          break;
        case "main/programs/state":
          try {
            const parsed = JSON.parse(payload);
            this.programs = parsed.programs || [];
            this.activeProgramIndex = parsed.activeProgram || 0;
          } catch (e) {
            /* ignorieren, Anzeige bleibt beim letzten gueltigen Stand */
          }
          break;
        case "diagnostics/i2cStatus":
          this.i2cStatus = payload;
          break;
        case "diagnostics/lastError":
          this.lastError = payload;
          break;
      }
    },

    // Farblogik deckungsgleich mit der Touch-UI-Ventilmatrix (HmiManager::refreshValveStatus()):
    // rot = state AN (ueberschreibt), gruen = auto AN + state AUS, dunkelgrau = auto AUS + state AUS.
    // V0 hat kein eigenes Automatik-Flag und wird nie gedimmt.
    valveState(v) {
      if (v.on) return "running";
      if (v.index === 0) return "auto";
      return v.auto ? "auto" : "idle";
    },

    valveMeta(v) {
      if (v.index === 0) return v.on ? "an" : "aus";
      if (v.on) return `${v.remaining} verbleibend`;
      if (!v.auto) return "nicht in Automatik";
      return `wartet · ${v.remaining}`;
    },

    heroHeadline() {
      if (!this.sequenceRunning) return "Automatik inaktiv";
      const idx = parseInt((this.activeValve || "").replace("V", ""), 10);
      const v = this.valves[idx];
      const name = v && v.alias ? v.alias : this.activeValve;
      return `${this.activeValve} · ${name} läuft`;
    },

    activeValveRemaining() {
      const idx = parseInt((this.activeValve || "").replace("V", ""), 10);
      const v = this.valves[idx];
      return v ? v.remaining : "00:00";
    },

    // Fortschritt des aktuell laufenden Ventils (nicht der ganzen Sequenz) - Basis ist die
    // Restlaufzeit, die beim Wechsel auf dieses Ventil zuerst gemeldet wurde.
    valveProgressPercent() {
      if (!this.sequenceRunning || !this.activeValveTotalSeconds) return 0;
      const pct = 100 - (parseMmSs(this.activeValveRemaining()) / this.activeValveTotalSeconds) * 100;
      return Math.max(0, Math.min(100, pct));
    },

    // Fortschritt der gesamten Automatik-Sequenz (main/remainingTotal) - Basis ist der Wert,
    // der beim Sequenzstart zuerst gemeldet wurde.
    sequenceProgressPercent() {
      if (!this.sequenceRunning || !this.sequenceTotalSeconds) return 0;
      const pct = 100 - (parseMmSs(this.remainingTotal) / this.sequenceTotalSeconds) * 100;
      return Math.max(0, Math.min(100, pct));
    },

    // "V1 10min · V2 10min automatisch" aus main/programs/state fuer das aktive Programm.
    programDetail() {
      if (!this.activeProgramIndex) return "";
      const prog = this.programs[this.activeProgramIndex - 1];
      if (!prog || !prog.auto) return "";
      const parts = Object.keys(prog.auto)
        .filter((k) => prog.auto[k])
        .sort()
        .map((k) => (prog.time && prog.time[k] ? `${k} ${prog.time[k]}min` : k));
      return parts.length ? `${parts.join(" · ")} automatisch` : "";
    },
  };
}
