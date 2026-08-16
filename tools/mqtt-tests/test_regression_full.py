#!/usr/bin/env python3
"""
Vollstaendiger, autonomer MQTT-Regressionstest gegen die echte Hardware.
Deckt die 10-Punkte-Checkliste aus docs/spec/12-aufraeumen.md ab, soweit per
MQTT/Software automatisierbar. Physische Punkte (I2C-Kabel ziehen, WLAN/Router
trennen) sind nicht automatisierbar und werden explizit als offen markiert.

Sichert vor dem Testlauf main/config/state + main/programs/state und stellt
beides am Ende wieder her (inkl. alle Ventile AUS).

ACHTUNG: schaltet echte Ventile (inkl. V0/Hauptventil) fuer kurze Zeit ein und
loest am Ende von Abschnitt 10 einen echten Hardware-Reset des Boards aus
(ueber esptool, kein Reflash). Nur gegen ein Board ausfuehren, bei dem das
unkritisch ist (z. B. kein Wasserdruck angeschlossen).

Voraussetzungen:
    pip install -r requirements.txt
    Board mit aktueller Firmware geflasht und online (WLAN+MQTT).
    Fuer den Persistenz-Test (Abschnitt 10): Board per USB an SERIAL_PORT.

Nutzung:
    python test_regression_full.py
"""

import json
import subprocess
import sys
import time

import paho.mqtt.client as mqtt

BROKER = "192.168.1.123"
PORT = 1883
PREFIX = "gartenwasser"
SERIAL_PORT = "COM8"

received = {}
events = []
results = []


def on_connect(c, u, f, rc, props=None):
    c.subscribe(f"{PREFIX}/#")


def on_message(c, u, msg):
    payload = msg.payload.decode("utf-8", errors="replace")
    received[msg.topic] = payload
    events.append((time.time(), msg.topic, payload))


def get(topic):
    return received.get(f"{PREFIX}/{topic}")


def get_json(topic):
    v = get(topic)
    return json.loads(v) if v is not None else None


def publish(topic, payload, retain=False):
    full = f"{PREFIX}/{topic}"
    print(f"  PUB {full} = {payload}")
    client.publish(full, payload, retain=retain)


def clear_events():
    events.clear()


def wait(seconds=1.2):
    time.sleep(seconds)


def check(name, condition, detail=""):
    status = "PASS" if condition else "FAIL"
    results.append((name, status, detail))
    marker = "OK    " if status == "PASS" else "FEHLER"
    line = f"  [{marker}] {name}"
    if detail:
        line += f"  ({detail})"
    print(line)


def section(title):
    print(f"\n{'=' * 70}\n{title}\n{'=' * 70}")


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="regression-test")
client.on_connect = on_connect
client.on_message = on_message

print(f"Verbinde mit {BROKER}:{PORT} ...")
client.connect(BROKER, PORT, keepalive=30)
client.loop_start()
wait(2.0)

if get("availability") != "online":
    print("Board ist nicht online - Abbruch.")
    client.loop_stop()
    sys.exit(2)

# --- Snapshot fuer Restore am Ende ------------------------------------------
config_snapshot = get("main/config/state")
programs_snapshot = get_json("main/programs/state")
active_program_snapshot = get_json("main/program/state")
print(f"Snapshot gesichert: config vorhanden={config_snapshot is not None}, "
      f"{len(programs_snapshot.get('programs', [])) if programs_snapshot else 0} Programme, "
      f"activeProgram={active_program_snapshot}")

# =============================================================================
section("1) Boot / Verfuegbarkeit")
check("availability = online", get("availability") == "online")
check("main/config/state vorhanden (retained)", config_snapshot is not None)

# =============================================================================
section("2) Ventile manuell (V1-V5) + V0-Kopplung")
publish("V1/cmd", "ON")
wait()
check("V1/state = ON", get("V1/state") == "ON")
check("V0/state = ON (Kopplung)", get("V0/state") == "ON")

publish("V2/cmd", "ON")
wait()
check("V2/state = ON", get("V2/state") == "ON")

publish("V1/cmd", "OFF")
wait()
check("V1/state = OFF", get("V1/state") == "OFF")
check("V0/state bleibt ON (V2 noch aktiv)", get("V0/state") == "ON")

publish("V2/cmd", "OFF")
wait()
check("V2/state = OFF", get("V2/state") == "OFF")
check("V0/state = OFF (kein Ventil mehr aktiv)", get("V0/state") == "OFF")

# =============================================================================
section("3) Laufzeit: time/set, maxTime-Deckelung, echter Zeitablauf")
publish("V3/time/set", "5")
wait()
check("V3/time/state = 5", get("V3/time/state") == "5")

original_max_time = get("main/time/maxTime")
publish("main/config/set", json.dumps({"maxTime": 1}))
wait()
check("maxTime auf 1 gesetzt", get("main/time/maxTime") == "1")

publish("V3/cmd", "ON")
wait(1.5)
remaining = get("V3/time/remaining")
check("V3/time/remaining auf maxTime gedeckelt (~1 Min., nicht 05:00)", remaining in ("01:00", "00:59"),
      f"remaining={remaining}")
publish("V3/cmd", "OFF")
wait()

publish("main/config/set", json.dumps({"maxTime": int(original_max_time)}))
wait()
check("maxTime zurueckgesetzt", get("main/time/maxTime") == original_max_time)

publish("V3/time/set", "1")
wait()
check("V3/time/state = 1", get("V3/time/state") == "1")
publish("V3/cmd", "ON")
wait(1.5)
check("V3/state = ON nach Einschalten", get("V3/state") == "ON")
print("  ... warte auf automatischen Zeitablauf (~65s) ...")
wait(68)
check("V3/state = OFF nach Zeitablauf (automatisch)", get("V3/state") == "OFF")
check("V3/time/remaining wieder auf 01:00 armiert (idle)", get("V3/time/remaining") == "01:00",
      f"remaining={get('V3/time/remaining')}")

# =============================================================================
section("4) Automatik-Flag (auto/set)")
publish("V4/auto/set", "ON")
wait()
check("V4/auto/state = ON", get("V4/auto/state") == "ON")
publish("V4/auto/set", "OFF")
wait()
check("V4/auto/state = OFF", get("V4/auto/state") == "OFF")

# =============================================================================
section("5) Automatik-Sequenz (main/cmd)")
publish("V1/auto/set", "ON")
publish("V2/auto/set", "ON")
publish("V3/auto/set", "OFF")
publish("V4/auto/set", "OFF")
publish("V5/auto/set", "OFF")
wait()

publish("main/cmd", "ON")
wait(1.5)
check("main/state = ON", get("main/state") == "ON")
check("main/activeValve = V1", get("main/activeValve") == "V1")
check("V1/state = ON (erstes Auto-Ventil)", get("V1/state") == "ON")
check("V0/state = ON waehrend Sequenz", get("V0/state") == "ON")

publish("V3/cmd", "ON")
wait()  # V3 hat auto=OFF, ist nicht Teil der Sequenz
check("manuelles V3/cmd ON waehrend Sequenz ignoriert", get("V3/state") == "OFF")

publish("V1/cmd", "OFF")
wait(1.5)  # aktives Ventil manuell aus -> Sequenz rueckt vor
check("main/activeValve = V2 (Sequenz vorgerueckt)", get("main/activeValve") == "V2")
check("V2/state = ON", get("V2/state") == "ON")
check("V1/time/remaining = 00:00 (Sequenz laeuft noch, nicht re-armiert)", get("V1/time/remaining") == "00:00")

publish("main/cmd", "OFF")
wait(1.5)
check("main/state = OFF nach Abbruch", get("main/state") == "OFF")
check("main/activeValve = - nach Abbruch", get("main/activeValve") == "-")
check("V2/state = OFF nach Abbruch", get("V2/state") == "OFF")
check("V0/state = OFF nach Abbruch", get("V0/state") == "OFF")
check("V1/time/remaining wieder armiert (Sequenz beendet)", get("V1/time/remaining") not in (None, "00:00"),
      f"remaining={get('V1/time/remaining')}")

# =============================================================================
section("6) Diagnostics (i2cStatus/lastError)")
check("diagnostics/i2cStatus = ok (Normalzustand)", get("diagnostics/i2cStatus") == "ok")
print("  [MANUELL/OFFEN] Fehlerfall (I2C-Kabel ziehen) ist physisch, nicht automatisierbar.")

# =============================================================================
section("7) Alias (inkl. V0, Umlaute, Validierung)")
publish("V1/alias/set", "Regressionstest Ä")
wait()
check("V1/alias uebernommen (inkl. Umlaut)", get("V1/alias") == "Regressionstest Ä", f"{get('V1/alias')}")

publish("V0/alias/set", "Hauptventil Test")
wait()
check("V0/alias uebernommen", get("V0/alias") == "Hauptventil Test")

too_long = "X" * 40  # > kAliasMaxLength (32)
before_alias = get("V1/alias")
publish("V1/alias/set", too_long)
wait()
check("zu langer Alias abgelehnt (unveraendert)", get("V1/alias") == before_alias, f"{get('V1/alias')}")

before_alias2 = get("V1/alias")
client.publish(f"{PREFIX}/V1/alias/set", b"bad\x01char", retain=False)
print(f"  PUB {PREFIX}/V1/alias/set = bad<0x01>char (raw bytes)")
wait()
check("Alias mit Steuerzeichen abgelehnt (unveraendert)", get("V1/alias") == before_alias2, f"{get('V1/alias')}")

# =============================================================================
section("8) Konfiguration per JSON (main/config/set)")
publish("main/config/set", json.dumps({"time": {"V5": 9}, "auto": {"V5": True}}))
wait()
check("Teil-Update: V5/time/state = 9", get("V5/time/state") == "9")
check("Teil-Update: V5/auto/state = ON", get("V5/auto/state") == "ON")
check("Teil-Update: andere Werte unberuehrt (V2/time/state weiterhin vorhanden)", get("V2/time/state") is not None)

publish("main/config/set", json.dumps({"time": {"V9": 5}}))
wait()
publish("V5/time/set", "9")
wait()
check("unbekannter Key 'V9' ignoriert, Board bleibt responsiv (kein Crash)", get("V5/time/state") == "9")

# =============================================================================
section("9) Resilienz (WLAN/MQTT-Verbindungsabbruch)")
print("  [MANUELL/OFFEN] Netzwerktrennung ist physisch und riskant fuer einen autonomen Testlauf.")

# =============================================================================
section("10) Persistenz ueber Neustart")
publish("V3/time/set", "2")
publish("V3/auto/set", "ON")
publish("V3/alias/set", "RebootMarker")
wait(1.0)
check("Marker vor Neustart gesetzt",
      get("V3/time/state") == "2" and get("V3/auto/state") == "ON" and get("V3/alias") == "RebootMarker")

print("  Loese Hardware-Reset via esptool aus (RTS-Pin, kein Reflash) ...")
clear_events()
try:
    proc = subprocess.run(
        ["esptool", "--port", SERIAL_PORT, "--chip", "esp32c6", "--after", "hard-reset", "read-mac"],
        capture_output=True, text=True, timeout=30,
    )
    print(f"  esptool exit={proc.returncode}")
except Exception as exc:  # noqa: BLE001 - Testskript, breiter Fang ok
    print(f"  esptool-Reset fehlgeschlagen: {exc}")

print("  Warte auf Reboot + Reconnect ...")
wait(10.0)
reconnect_ok = False
for _ in range(15):
    if get("availability") == "online":
        reconnect_ok = True
        break
    wait(1.0)
check("Board nach Neustart wieder online", reconnect_ok)

wait(1.5)
check("V3/time/state nach Neustart erhalten", get("V3/time/state") == "2", f"{get('V3/time/state')}")
check("V3/auto/state nach Neustart erhalten", get("V3/auto/state") == "ON", f"{get('V3/auto/state')}")
check("V3/alias nach Neustart erhalten", get("V3/alias") == "RebootMarker", f"{get('V3/alias')}")
all_off = all(get(f"V{v}/state") == "OFF" for v in range(0, 6))
check("alle Ventile nach Boot AUS", all_off, {f"V{v}": get(f"V{v}/state") for v in range(0, 6)})
check("keine Automatik-Sequenz nach Boot aktiv", get("main/state") == "OFF")
programs_after_reboot = get_json("main/programs/state")
expected_count = len(programs_snapshot.get("programs", [])) if programs_snapshot else 0
actual_count = len(programs_after_reboot.get("programs", [])) if programs_after_reboot else -1
check("Programme nach Neustart erhalten", actual_count == expected_count,
      f"erwartet={expected_count} tatsaechlich={actual_count}")

# =============================================================================
section("Aufraeumen: urspruenglichen Zustand wiederherstellen")
if config_snapshot:
    publish("main/config/set", config_snapshot)
    wait(1.5)
if programs_snapshot:
    publish("main/programs/set", json.dumps(programs_snapshot))
    wait(1.5)
if active_program_snapshot and active_program_snapshot.get("index"):
    publish("main/program/cmd", str(active_program_snapshot["index"]))
    wait(1.0)
for v in range(1, 6):
    publish(f"V{v}/cmd", "OFF")
wait(1.0)
print("Originalzustand wiederhergestellt (config, programs, alle Ventile AUS).")

# =============================================================================
print("\n" + "=" * 70)
passed = sum(1 for _, s, _ in results if s == "PASS")
failed = sum(1 for _, s, _ in results if s == "FAIL")
print(f"GESAMTERGEBNIS: {passed} bestanden, {failed} fehlgeschlagen (von {len(results)})")
if failed:
    print("\nFehlgeschlagene Checks:")
    for name, status, detail in results:
        if status == "FAIL":
            print(f"  - {name}: {detail}")

client.loop_stop()
client.disconnect()
sys.exit(1 if failed else 0)
