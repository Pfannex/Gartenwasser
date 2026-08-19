// Gartenwasser Web-Interface - Konfigurationsseite (Phase 18, Nachtrag: Auto-Buttons entfernt)
// Architektur B (siehe docs/spec/16-webif-fundament.md): eigene MQTT-over-WebSocket-
// Verbindung wie app.js, aber eigenes Datenmodell (time/auto/alias je Ventil, maxTime).
// Schreibpfade nur noch V{n}/alias/set, V{n}/time/set, main/config/set (maxTime) - "auto"
// wird nur noch lesend angezeigt (Ventilkachel-Farbe), gesetzt wird es ausschliesslich ueber
// Programme (siehe programme.js) - manuelle time/alias-Aenderungen setzen das aktive Programm
// firmwareseitig automatisch auf "MANUELL" zurueck (main/program/state, index=0).

const BROKER_WS_URL = "ws://192.168.1.123:9001/mqtt";
const TOPIC_PREFIX = "gartenwasser/";

function konfiguration() {
  return {
    connected: false,
    deviceOnline: false,
    valves: [0, 1, 2, 3, 4, 5].map((i) => ({
      index: i, alias: "", time: 0, auto: false, aliasPending: false, timePending: false,
    })),
    maxTime: 0,
    maxTimePending: false,
    footerVersion: "",

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
      const valveMatch = topic.match(/^V(\d)\/(alias|time\/state|auto\/state)$/);
      if (valveMatch) {
        const v = this.valves[parseInt(valveMatch[1], 10)];
        if (valveMatch[2] === "alias") {
          v.alias = payload;
          v.aliasPending = false;
        } else if (valveMatch[2] === "time/state") {
          v.time = parseInt(payload, 10) || 0;
          v.timePending = false;
        } else if (valveMatch[2] === "auto/state") {
          v.auto = payload === "ON";
        }
        return;
      }
      switch (topic) {
        case "availability":
          this.deviceOnline = payload === "online";
          break;
        case "main/time/maxTime":
          this.maxTime = parseInt(payload, 10) || 0;
          this.maxTimePending = false;
          break;
        case "diagnostics/version":
          this.footerVersion = payload;
          break;
      }
    },

    isCapped(v) {
      return this.maxTime > 0 && v.time > this.maxTime;
    },

    capLabel() {
      return `→ ${this.maxTime} min effektiv`;
    },

    // Feldfarbe: rot solange der geschriebene Wert noch nicht per Geraete-Echo
    // (.../state) bestaetigt ist, sonst - falls die Laufzeit dadurch gedeckelt wird -
    // gelb, sonst normal. Blendet ueber die CSS-Transition auf input weich zurueck.
    aliasClass(v) {
      return v.aliasPending ? "pending" : "";
    },
    timeClass(v) {
      if (v.timePending) return "pending";
      return this.isCapped(v) ? "time-exceeded" : "";
    },
    maxTimeClass() {
      return this.maxTimePending ? "pending" : "";
    },

    setAlias(v) {
      v.aliasPending = true;
      this.client.publish(TOPIC_PREFIX + `V${v.index}/alias/set`, v.alias);
    },

    setTime(v) {
      v.timePending = true;
      this.client.publish(TOPIC_PREFIX + `V${v.index}/time/set`, String(v.time));
    },

    setMaxTime() {
      this.maxTimePending = true;
      this.client.publish(TOPIC_PREFIX + "main/config/set", JSON.stringify({ maxTime: this.maxTime }));
    },
  };
}
