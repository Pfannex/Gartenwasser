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

// Fuer die "Naechster Termin"-Karte: JS Date.getDay() (0=So..6=Sa) auf die im Projekt
// verwendeten Wochentags-Keys (mon..sun, siehe docs/spec/15-wochenplan.md) abbilden.
const WEEKDAY_KEYS_BY_JS_DAY = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"];
const WEEKDAY_LABELS = { mon: "Mo", tue: "Di", wed: "Mi", thu: "Do", fri: "Fr", sat: "Sa", sun: "So" };

function atTime(date, time) {
  const [h, m] = (time || "00:00").split(":").map(Number);
  const d = new Date(date);
  d.setHours(h || 0, m || 0, 0, 0);
  return d;
}

function dashboard() {
  return {
    connected: false,
    deviceOnline: false,
    valves: [0, 1, 2, 3, 4, 5].map((i) => ({ index: i, on: false, auto: false, alias: "", remaining: "00:00", time: 0 })),
    sequenceRunning: false,
    activeValve: "-",
    remainingTotal: "00:00",
    sequenceTotalSeconds: null, // beim Sequenzstart erfasst, Basis fuer die Gesamt-Fortschrittsanzeige
    activeValveTotalSeconds: null, // beim Ventilwechsel erfasst, Basis fuer die Ventil-Fortschrittsanzeige
    activeProgramName: null,
    programs: [],
    activeProgramIndex: 0,
    pickerOpen: false,
    scheduleEntries: [],
    scheduleGlobalEnabled: true,
    i2cStatus: "ok",
    lastError: "",
    version: "",

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
      const valveMatch = topic.match(/^V(\d)\/(state|auto\/state|alias|time\/state|time\/remaining)$/);
      if (valveMatch) {
        const valve = this.valves[parseInt(valveMatch[1], 10)];
        if (valveMatch[2] === "state") valve.on = payload === "ON";
        else if (valveMatch[2] === "auto/state") valve.auto = payload === "ON";
        else if (valveMatch[2] === "alias") valve.alias = payload;
        else if (valveMatch[2] === "time/state") valve.time = parseInt(payload, 10) || 0;
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
        case "main/schedule/state":
          try {
            const parsed = JSON.parse(payload);
            this.scheduleEntries = parsed.schedule || [];
            this.scheduleGlobalEnabled = parsed.enabled !== false;
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
        case "diagnostics/version":
          this.version = payload;
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
      if (!v.auto) return `${v.time} min`;
      return `wartet · ${v.remaining}`;
    },

// "V1 · Apfel" (oder nur "V1" ohne Alias) - Basis fuer Headline und Ventil-Restlaufzeit-Label.
    activeValveLabel() {
      const idx = parseInt((this.activeValve || "").replace("V", ""), 10);
      const v = this.valves[idx];
      return v && v.alias ? `${this.activeValve} · ${v.alias}` : this.activeValve;
    },

    heroHeadline() {
      return `${this.activeValveLabel()} läuft`;
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

    // Naechster faelliger Zeitplan-Eintrag, komplett browserseitig berechnet - anders als
    // der 2026-08-17 verworfene "Kollisions-Hinweis" (echtzeit-blockierende Server-Pruefung
    // fuer main/cmd ON) ist das hier nur eine Anzeige, keine Firmware-Aenderung noetig.
    // Ignoriert: globalen Aus-Schalter, pausierte Eintraege, Eintraege mit einer Programm-
    // Referenz, die es nicht (mehr) gibt (analog zur "Programm nicht gefunden"-Warnung auf
    // der Zeitplanseite).
    nextSchedule() {
      if (!this.scheduleGlobalEnabled) return null;
      const now = new Date();
      let best = null;
      for (const entry of this.scheduleEntries) {
        if (entry.enabled === false) continue;
        if (!this.programs.some((p) => p.name === entry.program)) continue;
        const at = this.nextOccurrence(entry, now);
        if (at && (!best || at < best.at)) {
          best = { at, program: entry.program };
        }
      }
      if (!best) return null;
      return { program: best.program, when: this.formatWhen(best.at, now) };
    },

    nextOccurrence(entry, now) {
      if (entry.type === "daily") {
        const candidate = atTime(now, entry.time);
        if (candidate <= now) candidate.setDate(candidate.getDate() + 1);
        return candidate;
      }
      if (entry.type === "weekly") {
        const days = entry.weekdays || [];
        for (let offset = 0; offset < 8; offset++) {
          const d = new Date(now);
          d.setDate(d.getDate() + offset);
          if (!days.includes(WEEKDAY_KEYS_BY_JS_DAY[d.getDay()])) continue;
          const candidate = atTime(d, entry.time);
          if (candidate <= now) continue; // heutiger Wochentag, Uhrzeit aber schon vorbei
          return candidate;
        }
        return null;
      }
      if (entry.type === "once") {
        const candidate = atTime(entry.date, entry.time);
        return candidate > now ? candidate : null; // abgelaufen -> kein naechstes Mal
      }
      return null;
    },

    formatWhen(at, now) {
      const time = `${String(at.getHours()).padStart(2, "0")}:${String(at.getMinutes()).padStart(2, "0")}`;
      if (at.toDateString() === now.toDateString()) return `Heute · ${time}`;
      const tomorrow = new Date(now);
      tomorrow.setDate(tomorrow.getDate() + 1);
      if (at.toDateString() === tomorrow.toDateString()) return `Morgen · ${time}`;
      if ((at - now) / 86400000 < 7) return `${WEEKDAY_LABELS[WEEKDAY_KEYS_BY_JS_DAY[at.getDay()]]} · ${time}`;
      const dd = String(at.getDate()).padStart(2, "0");
      const mm = String(at.getMonth() + 1).padStart(2, "0");
      return `${dd}.${mm}.${at.getFullYear()} · ${time}`;
    },

    // Programm-Auswahlliste im Hero-Bereich: waehrend die Automatik laeuft gesperrt (ein
    // Wechsel waere bis zum naechsten Start ohnehin wirkungslos, siehe applyProgram()-Reapply
    // in MqttManager::startSequence() - analog zur bestehenden Sperre des Programme-Buttons
    // im Touch-UI waehrend Sequencer::isRunning()).
    togglePicker() {
      if (this.sequenceRunning) return;
      this.pickerOpen = !this.pickerOpen;
    },

    // Wendet sofort an (main/program/cmd), identisch zu "Aktivieren" auf der Programme-Seite
    // - kein Bestaetigungsschritt noetig, Start bleibt weiterhin ein separater Schritt.
    selectProgram(index) {
      this.client.publish(TOPIC_PREFIX + "main/program/cmd", String(index));
      this.pickerOpen = false;
    },

    // Start/Stop der Automatik-Sequenz - identisch zu main/cmd per MQTT/Touch-UI, keine
    // Sonderlogik. Die Firmware blockiert main/cmd ON ohnehin schon ohne gewaehltes
    // Programm (siehe MqttManager::startSequence()), hier also keine zusaetzliche Pruefung noetig.
    toggleSequence() {
      this.client.publish(TOPIC_PREFIX + "main/cmd", this.sequenceRunning ? "OFF" : "ON");
    },

    // Direktes Ventilschalten wie V{n}/cmd per MQTT/Touch-UI-Matrix. V0 hat keinen eigenen
    // cmd (Kopplung an V1-V5, siehe MqttManager) und bleibt ohne Funktion.
    toggleValve(v) {
      if (v.index === 0) return;
      this.client.publish(TOPIC_PREFIX + `V${v.index}/cmd`, v.on ? "OFF" : "ON");
    },
  };
}
