// Gartenwasser Web-Interface - Update-Seite (Phase 21, OTA)
// Bewusst OHNE MQTT-Verbindung: diese Seite spricht ausschliesslich mit dem eigenen
// Webserver des Geraets (echte HTTP-POST-Uploads, siehe WebIF.cpp) - anders als alle
// anderen Seiten, die fuer Live-Daten per MQTT-over-WebSocket mit dem Broker sprechen
// (Architektur B). Firmware/Dateisystem sind mit mehreren hundert KB bis 1-2 MB fuer
// MQTT (bei uns auf wenige KB gedeckelt) ohnehin die falsche Groessenordnung.

function ota() {
  return {
    firmwareFile: null,
    firmwareStatus: "idle", // idle | uploading | success | error
    firmwareProgress: 0,
    firmwareError: "",

    filesystemFile: null,
    filesystemStatus: "idle",
    filesystemProgress: 0,
    filesystemError: "",

    // target: "firmware" | "filesystem" - beide Ziele teilen sich dieselbe Upload-Logik,
    // nur Datei/Endpunkt/Status-Felder unterscheiden sich.
    upload(target) {
      const file = target === "firmware" ? this.firmwareFile : this.filesystemFile;
      if (!file) return;

      this[`${target}Status`] = "uploading";
      this[`${target}Progress`] = 0;
      this[`${target}Error`] = "";

      const formData = new FormData();
      formData.append(target, file);

      const xhr = new XMLHttpRequest();
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
          this[`${target}Progress`] = Math.round((e.loaded / e.total) * 100);
        }
      };
      xhr.onload = () => {
        if (xhr.status === 200) {
          this[`${target}Status`] = "success";
        } else {
          this[`${target}Status`] = "error";
          this[`${target}Error`] = "Update fehlgeschlagen (Details im Live-Log, Quelle OTA).";
        }
      };
      xhr.onerror = () => {
        this[`${target}Status`] = "error";
        this[`${target}Error`] = "Verbindung unterbrochen.";
      };
      xhr.open("POST", `/api/ota/${target}`);
      xhr.send(formData);
    },
  };
}
