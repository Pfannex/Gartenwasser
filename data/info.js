// Gartenwasser Web-Interface - Info-Seite (Phase 21-Folge, "Hardware-/Systeminfo")
// Architektur B (siehe docs/spec/16-webif-fundament.md): eigene MQTT-over-WebSocket-
// Verbindung wie die anderen Seiten. Kombiniert die bestehenden diagnostics/*-Topics
// (version/ram/flash, schon vom Dashboard genutzt) mit den neuen main/info/*-Topics.

const BROKER_WS_URL = "ws://192.168.1.123:9001/mqtt";
const TOPIC_PREFIX = "gartenwasser/";

function info() {
  return {
    connected: false,
    deviceOnline: false,
    version: "",
    ram: "",
    flash: "",
    resetReason: "",
    uptime: null,
    stackFree: null,
    rssi: null,
    ip: "",
    broker: "",
    partitions: [],

    init() {
      this.client = mqtt.connect(BROKER_WS_URL);
      this.client.on("connect", () => {
        this.connected = true;
        this.client.subscribe(TOPIC_PREFIX + "diagnostics/+");
        this.client.subscribe(TOPIC_PREFIX + "main/info/+");
        this.client.subscribe(TOPIC_PREFIX + "availability");
      });
      this.client.on("close", () => {
        this.connected = false;
      });
      this.client.on("message", (topic, payload) => this.handleMessage(topic.slice(TOPIC_PREFIX.length), payload.toString()));
    },

    handleMessage(topic, payload) {
      switch (topic) {
        case "availability":
          this.deviceOnline = payload === "online";
          break;
        case "diagnostics/version":
          this.version = payload;
          break;
        case "diagnostics/ram":
          this.ram = payload;
          break;
        case "diagnostics/flash":
          this.flash = payload;
          break;
        case "main/info/resetReason":
          this.resetReason = payload;
          break;
        case "main/info/uptime":
          this.uptime = parseInt(payload, 10);
          break;
        case "main/info/stackFree":
          this.stackFree = parseInt(payload, 10);
          break;
        case "main/info/rssi":
          this.rssi = parseInt(payload, 10);
          break;
        case "main/info/ip":
          this.ip = payload;
          break;
        case "main/info/broker":
          this.broker = payload;
          break;
        case "main/info/partitions":
          try {
            this.partitions = JSON.parse(payload);
          } catch (e) {
            /* ignorieren, Anzeige bleibt beim letzten gueltigen Stand */
          }
          break;
      }
    },

    // dd:hh:mm:ss, durchgehend 2-stellig gepolstert (Nutzerwunsch) - anders als anderswo im
    // Projekt (mm:ss fuer Restlaufzeiten) hier bewusst mit Tagen, da Uptime ueber Tage laufen kann.
    formatUptime(totalSeconds) {
      const pad = (n) => String(n).padStart(2, "0");
      const s = totalSeconds % 60;
      const m = Math.floor(totalSeconds / 60) % 60;
      const h = Math.floor(totalSeconds / 3600) % 24;
      const d = Math.floor(totalSeconds / 86400);
      return `${pad(d)}:${pad(h)}:${pad(m)}:${pad(s)}`;
    },

    // Boot-Zeitpunkt wird bewusst im Browser aus der Client-Uhr berechnet (Date.now() - Uptime)
    // statt vom Geraet per eigenem Topic zu kommen - spart eine weitere Publish-Quelle, das
    // Geraet kennt sein Echtzeit-Datum ohnehin erst nach erfolgreicher NTP-Synchronisierung.
    formatBootTime(totalSeconds) {
      const bootDate = new Date(Date.now() - totalSeconds * 1000);
      return bootDate.toLocaleString("de-DE");
    },

    formatKB(bytes) {
      return Math.round(bytes / 1024) + " KB";
    },
  };
}
