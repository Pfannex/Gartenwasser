#include "HaDiscovery.h"

#include <ArduinoJson.h>
#include <cstdio>

#include "MQTT.h"
#include "Version.h"
#include "BuildNumber.h"

namespace {

constexpr const char *kAvailabilityTopic = "gartenwasser/availability";
// Grosszuegige statische Obergrenze fuer den "number"-Slider (V{n}_time) - unabhaengig vom
// aktuellen main/time/maxTime (das ist ein Laufzeit-Wert, kein Compile-Zeit-Limit; die
// tatsaechlich wirksame Grenze bleibt ohnehin min(time, maxTime) auf der Firmware-Seite).
constexpr uint16_t kTimeSliderMaxMinutes = 180;
constexpr size_t kDiscoveryDocCapacity = 512;

// Gemeinsames device-Objekt (gruppiert alle Entities auf einer Geraeteseite in Home
// Assistant) + origin-Objekt (Firmware-Herkunft, seit einiger Zeit von Home Assistant
// empfohlen). Wird in jede einzelne Discovery-Config eingebettet, damit die Zuordnung
// unabhaengig von der Publish-Reihenfolge der 26 Entities funktioniert.
void addDeviceAndOrigin(StaticJsonDocument<kDiscoveryDocCapacity> &doc, const char *versionStr) {
  JsonObject dev = doc.createNestedObject("device");
  JsonArray ids = dev.createNestedArray("identifiers");
  ids.add("gartenwasser");
  // "ae" mit Hex-Escape statt woertlichem Umlaut im Quelltext (\xc3\xa4 = "ae" in UTF-8) -
  // ein woertlicher Umlaut fuehrte beim Kompilieren fuer den ESP32-C6-Toolchain zu einem
  // kaputten Byte in der publizierten Discovery-Config (ein U+FFFD-Ersatzzeichen statt "ae"),
  // vermutlich eine Quelltext-Encoding-Fehlannahme des Compilers. Mit expliziten Hex-Escapes
  // ist die tatsaechliche Byte-Sequenz unabhaengig davon eindeutig.
  dev["name"] = "Gartenbew\xc3\xa4sserung";
  dev["manufacturer"] = "Pf@nne";
  dev["sw_version"] = versionStr;

  JsonObject origin = doc.createNestedObject("origin");
  origin["name"] = "Gartenwasser Firmware";
  origin["sw_version"] = versionStr;
}

// Ergaenzt Name/unique_id/availability/device/origin und publiziert retained unter
// homeassistant/<component>/gartenwasser/<objectId>/config. object_id = unique_id (ohne
// "gartenwasser_"-Praefix bei unique_id waere ein Kollisionsrisiko mit fremden Integrationen
// auf demselben Broker) - siehe HA-Empfehlung "object_id auf unique_id setzen".
void finishAndPublish(StaticJsonDocument<kDiscoveryDocCapacity> &doc, const char *component, const char *objectId,
                       const char *name, const char *versionStr) {
  char uniqueId[40];
  snprintf(uniqueId, sizeof(uniqueId), "gartenwasser_%s", objectId);
  doc["name"] = name;
  doc["unique_id"] = uniqueId;
  doc["availability_topic"] = kAvailabilityTopic;
  addDeviceAndOrigin(doc, versionStr);

  char topic[96];
  snprintf(topic, sizeof(topic), "homeassistant/%s/gartenwasser/%s/config", component, objectId);
  char payload[kDiscoveryDocCapacity];
  serializeJson(doc, payload, sizeof(payload));
  MQTT::publish(topic, payload, true);
}

void publishBinarySensor(const char *objectId, const char *name, const char *stateTopic, const char *versionStr) {
  StaticJsonDocument<kDiscoveryDocCapacity> doc;
  doc["state_topic"] = stateTopic;
  finishAndPublish(doc, "binary_sensor", objectId, name, versionStr);
}

void publishSwitch(const char *objectId, const char *name, const char *cmdTopic, const char *stateTopic,
                    const char *versionStr) {
  StaticJsonDocument<kDiscoveryDocCapacity> doc;
  doc["command_topic"] = cmdTopic;
  doc["state_topic"] = stateTopic;
  finishAndPublish(doc, "switch", objectId, name, versionStr);
}

void publishSensor(const char *objectId, const char *name, const char *stateTopic, const char *versionStr) {
  StaticJsonDocument<kDiscoveryDocCapacity> doc;
  doc["state_topic"] = stateTopic;
  finishAndPublish(doc, "sensor", objectId, name, versionStr);
}

void publishNumber(const char *objectId, const char *name, const char *cmdTopic, const char *stateTopic,
                    const char *versionStr) {
  StaticJsonDocument<kDiscoveryDocCapacity> doc;
  doc["command_topic"] = cmdTopic;
  doc["state_topic"] = stateTopic;
  doc["min"] = 0;
  doc["max"] = kTimeSliderMaxMinutes;
  doc["unit_of_measurement"] = "min";
  finishAndPublish(doc, "number", objectId, name, versionStr);
}

}  // namespace

void HaDiscovery::publishAll() {
  char versionStr[32];
  snprintf(versionStr, sizeof(versionStr), "%s Build %s", kFirmwareVersion, kBuildNumber);

  publishBinarySensor("V0_state", "Hauptventil", "gartenwasser/V0/state", versionStr);

  char objectId[24];
  char name[32];
  char stateTopic[40];
  char cmdTopic[40];
  for (uint8_t i = 1; i <= 5; i++) {
    snprintf(objectId, sizeof(objectId), "V%u_cmd", i);
    snprintf(name, sizeof(name), "Ventil %u", i);
    snprintf(stateTopic, sizeof(stateTopic), "gartenwasser/V%u/state", i);
    snprintf(cmdTopic, sizeof(cmdTopic), "gartenwasser/V%u/cmd", i);
    publishSwitch(objectId, name, cmdTopic, stateTopic, versionStr);

    snprintf(objectId, sizeof(objectId), "V%u_auto", i);
    snprintf(name, sizeof(name), "Ventil %u Automatik", i);
    snprintf(stateTopic, sizeof(stateTopic), "gartenwasser/V%u/auto/state", i);
    // Bewusst binary_sensor statt switch: "auto" laesst sich seit der MANUELL-Regel nur noch
    // sinnvoll ueber Programme aendern, direktes Setzen waehlt implizit das aktive Programm
    // ab. Ein schaltbarer HA-switch waere hier ueberraschend (siehe docs/Log.md, Phase-10-
    // Vorueberlegung) - Aendern bleibt exklusiv ueber Touch/WebIF/main/program/cmd.
    publishBinarySensor(objectId, name, stateTopic, versionStr);

    snprintf(objectId, sizeof(objectId), "V%u_time", i);
    snprintf(name, sizeof(name), "Ventil %u Laufzeit", i);
    snprintf(stateTopic, sizeof(stateTopic), "gartenwasser/V%u/time/state", i);
    snprintf(cmdTopic, sizeof(cmdTopic), "gartenwasser/V%u/time/set", i);
    publishNumber(objectId, name, cmdTopic, stateTopic, versionStr);

    snprintf(objectId, sizeof(objectId), "V%u_remaining", i);
    snprintf(name, sizeof(name), "Ventil %u Restlaufzeit", i);
    snprintf(stateTopic, sizeof(stateTopic), "gartenwasser/V%u/time/remaining", i);
    publishSensor(objectId, name, stateTopic, versionStr);
  }

  publishSwitch("main_cmd", "Automatik-Sequenz", "gartenwasser/main/cmd", "gartenwasser/main/state", versionStr);
  publishSensor("main_activeValve", "Aktives Ventil", "gartenwasser/main/activeValve", versionStr);
  publishSensor("main_remainingTotal", "Restzeit Sequenz", "gartenwasser/main/remainingTotal", versionStr);
  publishSensor("diag_i2cStatus", "I2C-Status", "gartenwasser/diagnostics/i2cStatus", versionStr);
  publishSensor("diag_lastError", "Letzter Fehler", "gartenwasser/diagnostics/lastError", versionStr);
}
