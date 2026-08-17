// Gartenwasser Web-Interface - Status-Dashboard (Phase 17)
// Architektur B (siehe docs/spec/16-webif-fundament.md): der Browser verbindet sich
// direkt per MQTT-over-WebSocket mit dem Broker, nutzt exakt dieselben Topics wie
// mqtt-spy/Home Assistant - der ESP32 liefert nur diese statischen Dateien aus.

// Broker-Adresse fest hinterlegt (wie in tools/mqtt-tests/*.py) - anders als die
// Geraete-IP liegt der Broker in einem eigenen Netzsegment, kann nicht automatisch
// aus location.hostname abgeleitet werden.
const BROKER_WS_URL = "ws://192.168.1.123:9001/mqtt";
const TOPIC_PREFIX = "gartenwasser/";

function dashboard() {
  return {
    connected: false,
    deviceOnline: false,
    valves: [0, 1, 2, 3, 4, 5].map((i) => ({ index: i, on: false, auto: false, alias: "" })),
    sequenceRunning: false,
    activeValve: "-",
    remainingTotal: "00:00",
    activeProgramName: null,
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
      const valveMatch = topic.match(/^V(\d)\/(state|auto\/state|alias)$/);
      if (valveMatch) {
        const valve = this.valves[parseInt(valveMatch[1], 10)];
        if (valveMatch[2] === "state") valve.on = payload === "ON";
        else if (valveMatch[2] === "auto/state") valve.auto = payload === "ON";
        else if (valveMatch[2] === "alias") valve.alias = payload;
        return;
      }
      switch (topic) {
        case "availability":
          this.deviceOnline = payload === "online";
          break;
        case "main/state":
          this.sequenceRunning = payload === "ON";
          break;
        case "main/activeValve":
          this.activeValve = payload;
          break;
        case "main/remainingTotal":
          this.remainingTotal = payload;
          break;
        case "main/program/state":
          try {
            this.activeProgramName = JSON.parse(payload).name;
          } catch (e) {
            this.activeProgramName = null;
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
  };
}
