// Gartenwasser Web-Interface - Zeitplanverwaltung (Phase 20)
// Architektur B (siehe docs/spec/16-webif-fundament.md): eigene MQTT-over-WebSocket-
// Verbindung wie app.js/konfig.js/programme.js, Listen-Datenmodell wie Phase 19, aber
// program-Referenz per NAME statt Array-Index (main/schedule/*, siehe docs/spec/15-wochenplan.md)
// - anders als bei Programmen (Phase 19) gibt es hier deshalb keine Index-Verschiebung beim
// Loeschen zu beachten, main/schedule/set kann "schedule" unabhaengig von "enabled" schicken.

const BROKER_WS_URL = "ws://192.168.1.123:9001/mqtt";
const TOPIC_PREFIX = "gartenwasser/";
const MAX_ENTRIES = 16; // ConfigStore::kMaxScheduleEntries
const WEEKDAY_KEYS = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"];
const WEEKDAY_LABELS = { mon: "Mo", tue: "Di", wed: "Mi", thu: "Do", fri: "Fr", sat: "Sa", sun: "So" };

function todayIso() {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

function zeitplan() {
  return {
    connected: false,
    deviceOnline: false,
    programNames: [],
    entries: [],
    globalEnabled: true,
    editingIndex: null, // Index in entries[], "new", oder null
    editDraft: null,
    confirmDeleteIndex: null,
    maxEntries: MAX_ENTRIES,
    weekdayKeys: WEEKDAY_KEYS,

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
      switch (topic) {
        case "availability":
          this.deviceOnline = payload === "online";
          break;
        case "main/programs/state":
          try {
            this.programNames = (JSON.parse(payload).programs || []).map((p) => p.name);
          } catch (e) {
            /* ignorieren, Anzeige bleibt beim letzten gueltigen Stand */
          }
          break;
        case "main/schedule/state":
          try {
            const parsed = JSON.parse(payload);
            this.entries = parsed.schedule || [];
            this.globalEnabled = parsed.enabled !== false;
          } catch (e) {
            /* ignorieren, Anzeige bleibt beim letzten gueltigen Stand */
          }
          break;
      }
    },

    weekdayLabel(key) {
      return WEEKDAY_LABELS[key];
    },

    typeLabel(type) {
      return { daily: "Täglich", weekly: "Wöchentlich", once: "Einmalig" }[type] || type;
    },

    isExpired(entry) {
      return entry.type === "once" && entry.date < todayIso();
    },

    isBroken(entry) {
      return !this.programNames.includes(entry.program);
    },

    formatDate(iso) {
      const [y, m, d] = (iso || "").split("-");
      return d && m && y ? `${d}.${m}.${y}` : iso;
    },

    // "Jeden Tag · 21:00" / "Di, Fr · 20:00" / "01.02.2026 · 11:00" - HTML fuer die
    // "abgelaufen"-Markierung, daher x-html im Template.
    entryDetail(entry) {
      if (entry.type === "daily") return `Jeden Tag · ${entry.time}`;
      if (entry.type === "weekly") return `${(entry.weekdays || []).map((w) => WEEKDAY_LABELS[w]).join(", ")} · ${entry.time}`;
      const expired = this.isExpired(entry) ? ` <span class="expired">(abgelaufen)</span>` : "";
      return `${this.formatDate(entry.date)} · ${entry.time}${expired}`;
    },

    startEdit(idx) {
      const e = this.entries[idx];
      this.editDraft = {
        program: e.program,
        type: e.type,
        time: e.time,
        date: e.date || todayIso(),
        weekdays: e.weekdays ? [...e.weekdays] : [],
        enabled: e.enabled !== false,
      };
      this.editingIndex = idx;
      this.confirmDeleteIndex = null;
    },

    startNew() {
      if (this.entries.length >= this.maxEntries) return;
      this.editDraft = {
        program: this.programNames[0] || "",
        type: "daily",
        time: "20:00",
        date: todayIso(),
        weekdays: [],
        enabled: true,
      };
      this.editingIndex = "new";
    },

    cancelEdit() {
      this.editingIndex = null;
      this.editDraft = null;
    },

    toggleDraftWeekday(day) {
      const i = this.editDraft.weekdays.indexOf(day);
      if (i >= 0) this.editDraft.weekdays.splice(i, 1);
      else this.editDraft.weekdays.push(day);
    },

    saveEdit() {
      const d = this.editDraft;
      const entry = { enabled: d.enabled, type: d.type, time: d.time, program: d.program };
      if (d.type === "weekly") entry.weekdays = d.weekdays;
      if (d.type === "once") entry.date = d.date;

      if (this.editingIndex === "new") {
        this.entries.push(entry);
      } else {
        this.entries[this.editingIndex] = entry;
      }
      this.editingIndex = null;
      this.editDraft = null;
      this.publishSchedule();
    },

    requestDelete(idx) {
      if (this.confirmDeleteIndex === idx) {
        this.entries.splice(idx, 1);
        this.confirmDeleteIndex = null;
        if (this.editingIndex === idx) this.cancelEdit();
        this.publishSchedule();
      } else {
        this.confirmDeleteIndex = idx;
        setTimeout(() => {
          if (this.confirmDeleteIndex === idx) this.confirmDeleteIndex = null;
        }, 2500);
      }
    },

    // program-Referenz ist ein Name (kein Index) - anders als bei Programmen (Phase 19)
    // verschiebt ein Loeschen/Aendern hier also nichts anderes, "enabled" (global) kann
    // unveraendert bleiben (main/schedule/set-Teil-Update-Prinzip).
    publishSchedule() {
      this.client.publish(TOPIC_PREFIX + "main/schedule/set", JSON.stringify({ schedule: this.entries }));
    },

    toggleGlobal() {
      this.globalEnabled = !this.globalEnabled; // optimistisch, main/schedule/state bestaetigt gleich darauf
      this.client.publish(TOPIC_PREFIX + "main/schedule/cmd", this.globalEnabled ? "ON" : "OFF");
    },

    cleanupExpired() {
      this.entries = this.entries.filter((e) => !this.isExpired(e)); // optimistisch
      this.client.publish(TOPIC_PREFIX + "main/schedule/cleanup", "1");
    },
  };
}
