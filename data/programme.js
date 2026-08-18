// Gartenwasser Web-Interface - Programmverwaltung (Phase 19)
// Architektur B (siehe docs/spec/16-webif-fundament.md): eigene MQTT-over-WebSocket-
// Verbindung wie app.js/konfig.js, aber Listen-Datenmodell (main/programs/*) statt
// Einzelwerte - jede Aenderung schreibt das komplette Programme-Array zurueck
// (main/programs/set, Array-Replace-Prinzip), Aktivierung nutzt die schlanke
// main/program/cmd <index> (1-basiert, 0 = kein Programm).

const BROKER_WS_URL = "ws://192.168.1.123:9001/mqtt";
const TOPIC_PREFIX = "gartenwasser/";
const MAX_PROGRAMS = 32; // ConfigStore::kMaxPrograms

function programme() {
  return {
    connected: false,
    deviceOnline: false,
    valveAlias: { 1: "", 2: "", 3: "", 4: "", 5: "" },
    maxTime: 0,
    programs: [],
    activeProgram: 0,
    editingIndex: null, // Index in programs[], "new", oder null
    editDraft: null,
    confirmDeleteIndex: null,
    maxPrograms: MAX_PROGRAMS,

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
      const valveMatch = topic.match(/^V(\d)\/alias$/);
      if (valveMatch) {
        this.valveAlias[parseInt(valveMatch[1], 10)] = payload;
        return;
      }
      switch (topic) {
        case "availability":
          this.deviceOnline = payload === "online";
          break;
        case "main/time/maxTime":
          this.maxTime = parseInt(payload, 10) || 0;
          break;
        case "main/programs/state":
          try {
            const parsed = JSON.parse(payload);
            this.programs = parsed.programs || [];
            this.activeProgram = parsed.activeProgram || 0;
          } catch (e) {
            /* ignorieren, Anzeige bleibt beim letzten gueltigen Stand */
          }
          break;
      }
    },

    valveName(n) {
      return this.valveAlias[n] || `V${n}`;
    },

    // "V1 Apfel 10min · V2 Beete 10min" - gedeckelte Laufzeiten (time > maxTime) farblich
    // markiert, dieselbe Warnlogik wie auf der Konfigurationsseite (Phase 18).
    programDetail(p) {
      const parts = Object.keys(p.auto || {})
        .filter((k) => p.auto[k])
        .sort()
        .map((k) => {
          const n = k.replace("V", "");
          const t = (p.time && p.time[k]) || 0;
          const name = this.valveName(n);
          if (this.maxTime > 0 && t > this.maxTime) {
            return `${k} ${name} <span class="capped">${t}min → ${this.maxTime}min</span>`;
          }
          return `${k} ${name} ${t}min`;
        });
      return parts.length ? parts.join(" · ") : "keine Ventile ausgewählt";
    },

    isCapped(key) {
      return this.editDraft.auto[key] && this.editDraft.time[key] > this.maxTime;
    },

    blankDraft() {
      const auto = {}, time = {};
      for (let n = 1; n <= 5; n++) {
        auto["V" + n] = false;
        time["V" + n] = 5;
      }
      return { name: "", auto, time };
    },

    startEdit(idx) {
      const p = this.programs[idx];
      const auto = {}, time = {};
      for (let n = 1; n <= 5; n++) {
        const k = "V" + n;
        auto[k] = !!(p.auto && p.auto[k]);
        time[k] = (p.time && p.time[k]) || 5;
      }
      this.editDraft = { name: p.name, auto, time };
      this.editingIndex = idx;
      this.confirmDeleteIndex = null;
    },

    startNew() {
      if (this.programs.length >= this.maxPrograms) return;
      this.editDraft = this.blankDraft();
      this.editingIndex = "new";
    },

    cancelEdit() {
      this.editingIndex = null;
      this.editDraft = null;
    },

    saveEdit() {
      const name = (this.editDraft.name || "").trim() || "Unbenannt";
      const auto = { ...this.editDraft.auto };
      const time = {};
      Object.keys(auto).forEach((k) => {
        if (auto[k]) time[k] = this.editDraft.time[k];
      });
      const program = { name, time, auto };
      if (this.editingIndex === "new") {
        this.programs.push(program);
      } else {
        this.programs[this.editingIndex] = program;
      }
      this.editingIndex = null;
      this.editDraft = null;
      this.publishPrograms();
    },

    requestDelete(idx) {
      if (this.confirmDeleteIndex === idx) {
        this.programs.splice(idx, 1);
        if (this.activeProgram === idx + 1) this.activeProgram = 0;
        else if (this.activeProgram > idx + 1) this.activeProgram -= 1;
        this.confirmDeleteIndex = null;
        if (this.editingIndex === idx) this.cancelEdit();
        this.publishPrograms();
      } else {
        this.confirmDeleteIndex = idx;
        setTimeout(() => {
          if (this.confirmDeleteIndex === idx) this.confirmDeleteIndex = null;
        }, 2500);
      }
    },

    // Schickt "programs" IMMER zusammen mit dem aktuellen "activeProgram" - beim Loeschen
    // eines Programms vor dem aktiven verschieben sich dessen Array-Indizes (oben in
    // requestDelete() bereits lokal nachgefuehrt), main/programs/set repliziert sonst
    // (Teil-Update-Prinzip) nur "programs" und liesse den alten, jetzt falschen Index stehen.
    publishPrograms() {
      this.client.publish(TOPIC_PREFIX + "main/programs/set", JSON.stringify({
        programs: this.programs,
        activeProgram: this.activeProgram,
      }));
    },

    activate(n) {
      this.activeProgram = n; // optimistisch, main/programs/state bestaetigt gleich darauf
      this.client.publish(TOPIC_PREFIX + "main/program/cmd", String(n));
    },

    clearActive() {
      this.activate(0);
    },
  };
}
