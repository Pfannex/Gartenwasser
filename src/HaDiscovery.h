/**
 * @file    HaDiscovery.h
 * @brief   Home-Assistant-MQTT-Discovery (Phase 10). Baut fuer jede Entity aus der
 *          Mapping-Tabelle in docs/requirements.md eine Discovery-Config (JSON) und
 *          publiziert sie retained unter homeassistant/<component>/gartenwasser/<object_id>/config,
 *          siehe docs/spec/10-ha-discovery.md.
 */

#pragma once

namespace HaDiscovery {

/// Publiziert alle Discovery-Configs (retained). Wird nach jedem erfolgreichen
/// MQTT-Connect erneut aufgerufen (siehe MQTT.cpp, connectToBroker()) - Discovery-Configs
/// sind zwar retained, aber ein Re-Publish nach jedem Reconnect stellt sicher, dass Home
/// Assistant die Entities auch nach einem eigenen Neustart (leere Datenbank) wiederfindet.
void publishAll();

}  // namespace HaDiscovery
