#!/usr/bin/env python3
"""
Automatisierter MQTT-Test fuer Phase 14 (Bewaesserungsprogramme).
Deckt die Testfaelle aus docs/spec/14-programme.md ab, plus einen Bonus-Test
fuer die Teilmengen-Semantik (Kernentscheidung 2: Programme ohne Angabe fuer
ein Ventil lassen dessen Zustand unveraendert).

Voraussetzung: Board mit aktueller Firmware geflasht und online (WLAN+MQTT).

Nutzung:
    python test_phase14.py
"""

import json
import sys
import time

import paho.mqtt.client as mqtt

BROKER = "192.168.1.123"
PORT = 1883
PREFIX = "gartenwasser"

received = {}   # topic -> letzter Payload-String
events = []     # (topic, payload) seit letztem clear_events(), in Ankunftsreihenfolge
results = []    # (name, "PASS"/"FAIL", detail)


def on_connect(client, userdata, flags, reason_code, properties=None):
    client.subscribe(f"{PREFIX}/#")


def on_message(client, userdata, msg):
    payload = msg.payload.decode("utf-8", errors="replace")
    received[msg.topic] = payload
    events.append((msg.topic, payload))


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
    marker = "OK  " if status == "PASS" else "FEHLER"
    line = f"  [{marker}] {name}"
    if detail:
        line += f"  ({detail})"
    print(line)


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="phase14-test")
client.on_connect = on_connect
client.on_message = on_message

print(f"Verbinde mit {BROKER}:{PORT} ...")
try:
    client.connect(BROKER, PORT, keepalive=30)
except Exception as exc:  # noqa: BLE001 - Testskript, breiter Fang ok
    print(f"Verbindung fehlgeschlagen: {exc}")
    sys.exit(2)

client.loop_start()
wait(1.5)  # retained Messages initial einsammeln

if get("availability") != "online":
    print("Board ist NICHT online (gartenwasser/availability != 'online').")
    print("Bitte zuerst flashen/booten lassen und WLAN/MQTT verbinden, dann erneut starten.")
    client.loop_stop()
    sys.exit(1)

print("Board online. Starte Tests.\n")

# --- Setup + Test 6: main/programs/set ersetzt das komplette Array ---------
print("Setup / Test 6: main/programs/set (Bulk-Replace)")
programs_payload = {
    "programs": [
        {"name": "Kurz", "time": {"V1": 2, "V2": 2},
         "auto": {"V1": True, "V2": True, "V3": False, "V4": False, "V5": False}},
        {"name": "Rasen", "time": {"V1": 10, "V2": 10},
         "auto": {"V1": True, "V2": True, "V3": False, "V4": False, "V5": False}},
        {"name": "Alles", "time": {"V1": 8, "V2": 8, "V3": 12, "V4": 15, "V5": 6},
         "auto": {"V1": True, "V2": True, "V3": True, "V4": True, "V5": True}},
        {"name": "Test", "time": {"V1": 1, "V2": 1, "V3": 1, "V4": 1, "V5": 1},
         "auto": {"V1": True, "V2": True, "V3": True, "V4": True, "V5": True}},
    ]
}
config_before = get("main/config/state")
clear_events()
publish("main/programs/set", json.dumps(programs_payload))
wait(3.0)  # groesserer Payload kurz nach dem Boot kann laenger brauchen als 1.5s
programs_state = get_json("main/programs/state")
check("Array ersetzt (main/programs/state hat 4 Programme)",
      programs_state is not None and len(programs_state.get("programs", [])) == 4,
      f"programs/state={programs_state}")
check("main/config/state unveraendert (config/programs getrennt)",
      get("main/config/state") == config_before)

# --- Test 1: main/program/cmd 4 --------------------------------------------
print("\nTest 1: main/program/cmd 4 (Programm 'Test' anwenden)")
clear_events()
publish("main/program/cmd", "4")
wait(1.5)
prog_state = get_json("main/program/state")
check("main/program/state = index 4 / 'Test'", prog_state == {"index": 4, "name": "Test"}, f"{prog_state}")
times_ok = all(get(f"V{v}/time/state") == "1" for v in range(1, 6))
check("alle V1-V5 time/state = 1", times_ok, {f"V{v}": get(f"V{v}/time/state") for v in range(1, 6)})
auto_ok = all(get(f"V{v}/auto/state") == "ON" for v in range(1, 6))
check("alle V1-V5 auto/state = ON", auto_ok, {f"V{v}": get(f"V{v}/auto/state") for v in range(1, 6)})

# --- Test 2: manuelle Aenderung waehrend Programm gewaehlt ------------------
print("\nTest 2: manuelles V2/time/set waehrend Programm 4 aktiv")
clear_events()
publish("V2/time/set", "7")
wait(1.0)
check("V2/time/state = 7", get("V2/time/state") == "7")
prog_state2 = get_json("main/program/state")
check("Programmwahl bleibt bei 4 (kein Lock)", prog_state2 == {"index": 4, "name": "Test"}, f"{prog_state2}")

# --- Test 3: ungueltiger Index ----------------------------------------------
print("\nTest 3: main/program/cmd 99 (ungueltig)")
before = get("main/program/state")
clear_events()
publish("main/program/cmd", "99")
wait(1.0)
check("main/program/state unveraendert", get("main/program/state") == before)

# --- Test 4: main/program/cmd 0 ---------------------------------------------
print("\nTest 4: main/program/cmd 0 (Auswahl loeschen)")
v_times_before = {v: get(f"V{v}/time/state") for v in range(1, 6)}
clear_events()
publish("main/program/cmd", "0")
wait(1.0)
prog_state4 = get_json("main/program/state")
check("main/program/state = index 0 / name null", prog_state4 == {"index": 0, "name": None}, f"{prog_state4}")
v_times_after = {v: get(f"V{v}/time/state") for v in range(1, 6)}
check("Ventile unveraendert (cmd 0 fasst keine Ventile an)", v_times_before == v_times_after,
      f"vorher={v_times_before} nachher={v_times_after}")

# --- Bonus: Teilmengen-Semantik (Kernentscheidung 2) ------------------------
# Echte Teilmenge: ein Programm, das V2-V5 in KEINEM der beiden Felder (time/auto)
# erwaehnt, muss sie unangetastet lassen. ("Kurz" aus dem Setup-Array ist dafuer
# kein gutes Beispiel: es setzt auto explizit fuer V3-V5 auf false, siehe Spec-Beispiel.)
print("\nBonus: Programm mit echter Teilmenge (nur V1) laesst V2-V5 unangetastet")
publish("main/program/cmd", "3")  # "Alles" -> bekannter Zustand fuer V2-V5
wait(1.5)
v2345_before = {v: (get(f"V{v}/time/state"), get(f"V{v}/auto/state")) for v in (2, 3, 4, 5)}
mini_payload = {"programs": [{"name": "Mini", "time": {"V1": 9}, "auto": {"V1": True}}]}
clear_events()
publish("main/programs/set", json.dumps(mini_payload))
wait(2.0)
publish("main/program/cmd", "1")
wait(1.5)
prog_state5 = get_json("main/program/state")
check("main/program/state = index 1 / 'Mini'", prog_state5 == {"index": 1, "name": "Mini"}, f"{prog_state5}")
check("V1/time/state = 9", get("V1/time/state") == "9")
check("V1/auto/state = ON", get("V1/auto/state") == "ON")
v2345_after = {v: (get(f"V{v}/time/state"), get(f"V{v}/auto/state")) for v in (2, 3, 4, 5)}
check("V2-V5 unveraendert (in 'Mini' ueberhaupt nicht erwaehnt)", v2345_before == v2345_after,
      f"vorher={v2345_before} nachher={v2345_after}")

# --- Aufraeumen: sicheren Zustand herstellen --------------------------------
print("\nAufraeumen: Programmwahl loeschen, Automatik-Flags zuruecksetzen")
publish("main/program/cmd", "0")
for v in range(1, 6):
    publish(f"V{v}/auto/set", "OFF")
wait(1.0)

# --- Zusammenfassung ---------------------------------------------------------
print("\n" + "=" * 60)
passed = sum(1 for _, status, _ in results if status == "PASS")
failed = sum(1 for _, status, _ in results if status == "FAIL")
print(f"Ergebnis: {passed} bestanden, {failed} fehlgeschlagen (von {len(results)})")
if failed:
    print("\nFehlgeschlagene Tests:")
    for name, status, detail in results:
        if status == "FAIL":
            print(f"  - {name}: {detail}")

client.loop_stop()
client.disconnect()
sys.exit(1 if failed else 0)
