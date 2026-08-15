# Phase 9 — Alias je Ventil

**Status:** ✅ Erledigt & getestet

## Ziel

Editierbarer Klartextname je Ventil, persistent über Neustart hinweg.

## Voraussetzungen

- Phase 4 (Ventile per MQTT) ✅
- Phase 5 (`ConfigStore` existiert bereits) ✅

## Umsetzung

- `alias/set` → `alias` (retained Text) je Ventil, inkl. `V0` (nachträglich ergänzt — ursprünglich nur `V1`–`V5` spezifiziert).
- Persistenz via `ConfigStore` (SPIFFS, siehe Phase 5).
- Validierung: max. `ConfigStore::kAliasMaxLength` (32) Zeichen **und** keine Steuerzeichen (Bytes < 0x20). UTF-8-Mehrbyte-Folgen (Umlaute etc.) sind ausdrücklich erlaubt.

## Betroffene Dateien

- `src/ValveController.h/.cpp`
- `src/ConfigStore.h/.cpp` (Erweiterung um `alias`-Werte, inkl. V0)
- `src/MqttManager.h/.cpp` (Subscribe-Handler, wie in vorherigen Phasen nicht in der ursprünglichen Liste, aber notwendig)

## MQTT-Topics

- `gartenwasser/V{0..5}/alias/set` (subscribe)
- `gartenwasser/V{0..5}/alias` (publish, retained)

## Test

1. `mosquitto_pub -t gartenwasser/V2/alias/set -m "Rasen Seite"` → `alias`-Topic wird retained aktualisiert.
2. Neustart des Boards → Alias bleibt erhalten (Persistenz-Test).

## Test / Ergebnis

- Auf Hardware getestet (inkl. V0-Nachtrag), vom Nutzer bestätigt ("klapp!").

## Gefundener Bug (während der Umsetzung)

- Die Längenvalidierung im MQTT-Handler prüfte zunächst den bereits von `copyPayload()` auf die Puffergröße gekürzten String — ein zu langer Alias wäre dadurch statt abgelehnt einfach still gekürzt worden. Fix: Validierung nutzt die ursprüngliche MQTT-Payload-Länge (Parameter aus dem PubSubClient-Callback), nicht `strlen()` der gekürzten Kopie.

## Nachtrag: Alias für V0 (2026-08-15)

- `V0` (Hauptventil) hatte ursprünglich keinen Alias (Spec war auf `V1`–`V5` begrenzt). Ergänzt: `ConfigStore::getValveAlias()`/`setValveAlias()` erlauben jetzt Index `0..5` (statt `1..5`), `V0/alias/set` wird in `MqttManager` separat behandelt (nicht über `parseValveTopic()`, das bewusst auf `V1..V5` begrenzt bleibt, da `V0` kein `cmd`/`time`/`auto` hat).
