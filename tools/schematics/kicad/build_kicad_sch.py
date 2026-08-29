import uuid

MM = 1.0  # kicad_sch native unit is mm
PITCH = 2.54  # 100mil pin grid
PIN_LEN = 5.08  # 200mil pin lead length

def u():
    return str(uuid.uuid4())

def esc(s):
    return s.replace('"', '\\"')

# ---- pin data (name, pin_number_or_None) ----
esp_left = [
    ('VBUS', 'VBUS'), ('GND', 'GND1'), ('GPIO16', 'UART0_TX'), ('GPIO17', 'UART0_RX'),
    ('RST', 'RST'), ('GPIO1', 'SPI_SCLK'), ('GPIO2', 'SPI_MOSI'), ('GPIO3', 'SPI_MISO'),
    ('GPIO4', None), ('GPIO5', None), ('GPIO6', None),
]
esp_right = [
    ('VBAT', 'VBAT'), ('GND_2', 'GND2'), ('GND_3', 'GND3'), ('V3V3', '3V3'),
    ('GPIO19', 'I2C_SCL'), ('GPIO18', 'I2C_SDA'), ('GPIO13', 'USB_DP'), ('GPIO12', 'USB_DN'),
    ('GPIO9', None), ('GPIO8', None), ('GPIO7', None),
]

mcp_left = [
    ('GPB0', '1'), ('GPB1', '2'), ('GPB2', '3'), ('GPB3', '4'),
    ('GPB4', '5'), ('GPB5', '6'), ('GPB6', '7'), ('GPB7', '8'),
    ('VDD', '9'), ('VSS', '10'), ('NC1', '11'), ('SCL', '12'), ('SDA', '13'), ('NC2', '14'),
]
mcp_right = [
    ('A0', '15'), ('A1', '16'), ('A2', '17'), ('RESET', '18'),
    ('INTB', '19'), ('INTA', '20'), ('GPA0', '21'), ('GPA1', '22'),
    ('GPA2', '23'), ('GPA3', '24'), ('GPA4', '25'), ('GPA5', '26'), ('GPA6', '27'), ('GPA7', '28'),
]

# Physisches Modul-Layout (Foto): oben HV1 HV2 HV GND HV3 HV4, unten LV1 LV2 LV GND LV3 LV4
# (kurze numerische Pin-Nummern statt Namens-Fallback, sonst ueberlappt bei
# senkrechten Pins der Name-Text mit dem Nummer-Text)
ls_top = [('HV1', '1'), ('HV2', '2'), ('HV', '3'), ('GND2', '4'), ('HV3', '5'), ('HV4', '6')]
ls_bottom = [('LV1', '7'), ('LV2', '8'), ('LV', '9'), ('GND1', '10'), ('LV3', '11'), ('LV4', '12')]

relay_left = [('V0', 'OUT0'), ('V1', 'OUT1'), ('V2', 'OUT2'), ('V3', 'OUT3'), ('V4', 'OUT4'), ('V5', 'OUT5')]
relay_right = [('IN0', None), ('IN1', None), ('IN2', None), ('IN3', None), ('IN4', None), ('IN5', None)]


class IcDef:
    """Builds a lib_symbols entry + records local pin offsets (library Y-up space).

    left/right pins are stacked vertically (standard IC style); top/bottom pins
    are spread horizontally (header/module style, e.g. the level-shifter board).
    Both can be combined on the same symbol if needed.
    """

    def __init__(self, libname, symname, ref_prefix, left=(), right=(), width=None,
                 top=(), bottom=(), height=None, value=None, types=None, display=None):
        self.libname = libname
        self.symname = symname
        self.ref_prefix = ref_prefix
        self.value = value or symname
        self.types = types or {}
        self.display = display or {}
        n_lr = max(len(left), len(right))
        self.height = height if height is not None else (n_lr + 1) * PITCH
        half_h = self.height / 2
        n_tb = max(len(top), len(bottom))
        min_width_tb = (n_tb - 1) * PITCH + 2 * PITCH if n_tb else 0
        self.width = max(width or 0, min_width_tb)
        half_w = self.width / 2
        self.local = {}  # pin_name -> (x, y) library space (Y-up)
        self.pin_num_map = {}
        self.pin_lines = []
        for i, (name, num) in enumerate(left):
            y = half_h - PITCH * (i + 1)
            x = -half_w - PIN_LEN
            self.local[name] = (x, y)
            self.pin_num_map[name] = num or name
            self.pin_lines.append(self._pin(name, num or name, x, y, 0))
        # Right side: reversed order, so pin count N (last item) sits at the TOP
        # and pin count N/2+1 (first item) sits at the BOTTOM -- matches the
        # standard DIP/SOIC wraparound numbering (pin 1 top-left, counting down
        # the left side, then back up the right side to pin N at top-right).
        for i, (name, num) in enumerate(reversed(right)):
            y = half_h - PITCH * (i + 1)
            x = half_w + PIN_LEN
            self.local[name] = (x, y)
            self.pin_num_map[name] = num or name
            self.pin_lines.append(self._pin(name, num or name, x, y, 180))
        for i, (name, num) in enumerate(top):
            x = -((n_tb - 1) * PITCH) / 2 + i * PITCH
            y = half_h + PIN_LEN
            self.local[name] = (x, y)
            self.pin_num_map[name] = num or name
            self.pin_lines.append(self._pin(name, num or name, x, y, 270))
        for i, (name, num) in enumerate(bottom):
            x = -((n_tb - 1) * PITCH) / 2 + i * PITCH
            y = -half_h - PIN_LEN
            self.local[name] = (x, y)
            self.pin_num_map[name] = num or name
            self.pin_lines.append(self._pin(name, num or name, x, y, 90))
        self.half_w = half_w
        self.half_h = half_h

    def _pin(self, name, num, x, y, angle):
        etype = self.types.get(name, 'bidirectional')
        label = self.display.get(name, name)
        return (
            f'\t\t\t\t(pin {etype} line (at {x:.2f} {y:.2f} {angle}) (length {PIN_LEN:.2f})\n'
            f'\t\t\t\t\t(name "{esc(label)}" (effects (font (size 1.27 1.27))))\n'
            f'\t\t\t\t\t(number "{esc(num)}" (effects (font (size 1.27 1.27))))\n'
            f'\t\t\t\t)\n'
        )

    def lib_text(self):
        pins = ''.join(self.pin_lines)
        # pin-1 marker: small filled dot at the top-left corner of the body
        dot_r = 0.5
        dot_x = -self.half_w + dot_r + 0.3
        dot_y = self.half_h - dot_r - 0.3
        marker = (
            f'\t\t\t\t(circle (center {dot_x:.2f} {dot_y:.2f}) (radius {dot_r:.2f})\n'
            f'\t\t\t\t\t(stroke (width 0) (type default)) (fill (type outline)))\n'
        )
        return (
            f'\t\t(symbol "{self.libname}:{self.symname}"\n'
            f'\t\t\t(in_bom yes) (on_board yes)\n'
            f'\t\t\t(property "Reference" "{self.ref_prefix}" (at 0 {self.half_h + 2.54:.2f} 0) (effects (font (size 1.27 1.27))))\n'
            f'\t\t\t(property "Value" "{esc(self.value)}" (at 0 {-(self.half_h + 2.54):.2f} 0) (effects (font (size 1.27 1.27))))\n'
            f'\t\t\t(symbol "{self.symname}_0_1"\n'
            f'\t\t\t\t(rectangle (start {-self.half_w:.2f} {self.half_h:.2f}) (end {self.half_w:.2f} {-self.half_h:.2f})\n'
            f'\t\t\t\t\t(stroke (width 0.254) (type default)) (fill (type background)))\n'
            f'{marker}'
            f'\t\t\t)\n'
            f'\t\t\t(symbol "{self.symname}_1_1"\n'
            f'{pins}'
            f'\t\t\t)\n'
            f'\t\t)\n'
        )


class Placed:
    """A symbol instance placed on the sheet, with sheet-space pin lookup."""

    def __init__(self, icdef, ref, sx, sy):
        self.icdef = icdef
        self.ref = ref
        self.sx = sx
        self.sy = sy
        self.uuid = u()

    def pin_pos(self, name):
        lx, ly = self.icdef.local[name]
        return (self.sx + lx, self.sy - ly)  # library Y-up -> sheet Y-down

    def instance_text(self):
        d = self.icdef
        pin_uuids = {name: u() for name in d.local}
        self._pin_uuids = pin_uuids
        pins_txt = ''.join(
            f'\t\t(pin "{esc(d.pin_num_map[name])}" (uuid {pin_uuids[name]}))\n' for name in d.local
        )
        return (
            f'\t(symbol\n'
            f'\t\t(lib_id "{d.libname}:{d.symname}")\n'
            f'\t\t(at {self.sx:.2f} {self.sy:.2f} 0)\n'
            f'\t\t(unit 1)\n'
            f'\t\t(in_bom yes) (on_board yes) (dnp no)\n'
            f'\t\t(uuid {self.uuid})\n'
            f'\t\t(property "Reference" "{self.ref}" (at {self.sx:.2f} {self.sy - d.half_h - 2.54:.2f} 0) (effects (font (size 1.27 1.27))))\n'
            f'\t\t(property "Value" "{esc(d.value)}" (at {self.sx:.2f} {self.sy + d.half_h + 2.54:.2f} 0) (effects (font (size 1.27 1.27))))\n'
            f'{pins_txt}'
            f'\t)\n'
        )




def wire(p1, p2):
    return f'\t(wire (pts (xy {p1[0]:.2f} {p1[1]:.2f}) (xy {p2[0]:.2f} {p2[1]:.2f})) (stroke (width 0.1524) (type default)) (uuid {u()}))\n'


def elbow_wire(p1, p2, via_x=None, via_y=None):
    """Two-segment orthogonal route: horizontal then vertical (or via a fixed via_x)."""
    if via_x is not None:
        mid = (via_x, p1[1])
        mid2 = (via_x, p2[1])
        return wire(p1, mid) + wire(mid, mid2) + wire(mid2, p2)
    mid = (p2[0], p1[1])
    return wire(p1, mid) + wire(mid, p2)


def global_label(text, pos, angle, shape='input'):
    justify = 'left' if angle == 0 else 'right'
    return (
        f'\t(global_label "{esc(text)}" (shape {shape}) (at {pos[0]:.2f} {pos[1]:.2f} {angle})\n'
        f'\t\t(effects (font (size 1.27 1.27)) (justify {justify}))\n'
        f'\t\t(uuid {u()})\n'
        f'\t)\n'
    )


def power_stub(pin_pos, text, direction, length=7.62, shape='input'):
    """direction: 'left','right','up','down' -- which way the stub extends away from the pin."""
    dx, dy, angle = {
        'left': (-length, 0, 180),
        'right': (length, 0, 0),
        'up': (0, -length, 90),
        'down': (0, length, 270),
    }[direction]
    end = (pin_pos[0] + dx, pin_pos[1] + dy)
    txt = wire(pin_pos, end)
    txt += global_label(text, end, angle, shape=shape)
    return txt


# ---------------- electrical pin types (default: bidirectional) ----------------
POWER = {'VBUS': 'power_in', 'GND': 'power_in', 'GND_2': 'power_in', 'GND_3': 'power_in', 'VBAT': 'power_in', 'V3V3': 'power_in'}
esp_types = dict(POWER)

mcp_types = {
    'VDD': 'power_in', 'VSS': 'power_in',
    'NC1': 'no_connect', 'NC2': 'no_connect',
    'A0': 'input', 'A1': 'input', 'A2': 'input', 'RESET': 'input',
    'INTB': 'output', 'INTA': 'output',
}

ls_types = {'LV': 'power_in', 'HV': 'power_in', 'GND1': 'power_in', 'GND2': 'power_in'}
ls_display = {'GND1': 'GND', 'GND2': 'GND'}

relay_types = {f'V{i}': 'output' for i in range(6)}  # OUT0..OUT5 (named V0..V5)
relay_types.update({f'IN{i}': 'input' for i in range(6)})

# ---------------- build the components ----------------
esp = IcDef('gartenwasser', 'ESP32C6_TouchLCD', 'A', esp_left, esp_right, width=32, value='Waveshare ESP32-C6-Touch-LCD-1.47', types=esp_types)
mcp = IcDef('gartenwasser', 'MCP23017', 'U', mcp_left, mcp_right, width=28, value='MCP23017', types=mcp_types)
ls = IcDef('gartenwasser', 'LevelShifter4Ch', 'U', top=ls_top, bottom=ls_bottom, height=20,
           value='I2C Level-Shifter (4-Kanal, bidirektional)', types=ls_types, display=ls_display)
relay = IcDef('gartenwasser', 'Relay6Ch', 'K', relay_left, relay_right, width=26, value='Relaismodul (6 Kanaele)', types=relay_types)

lib_symbols_text = esp.lib_text() + mcp.lib_text() + ls.lib_text() + relay.lib_text()

P_ESP = Placed(esp, 'A1', 60, 150)
P_MCP = Placed(mcp, 'U1', 260, 150)
P_RELAY = Placed(relay, 'K1', 170, 60)
P_LS = Placed(ls, 'U2', 160, 200)

instances_text = P_ESP.instance_text() + P_MCP.instance_text() + P_RELAY.instance_text() + P_LS.instance_text()

wires_text = ''
# I2C through the level shifter
wires_text += elbow_wire(P_ESP.pin_pos('GPIO19'), P_LS.pin_pos('LV1'))
wires_text += elbow_wire(P_ESP.pin_pos('GPIO18'), P_LS.pin_pos('LV2'))
wires_text += elbow_wire(P_LS.pin_pos('HV1'), P_MCP.pin_pos('SCL'), via_x=230)
wires_text += elbow_wire(P_LS.pin_pos('HV2'), P_MCP.pin_pos('SDA'), via_x=234)

# valve relay wiring: GPB7=Hauptventil(V0), GPB2..GPB6=V1..V5
wires_text += elbow_wire(P_MCP.pin_pos('GPB7'), P_RELAY.pin_pos('IN0'), via_x=220)
wires_text += elbow_wire(P_MCP.pin_pos('GPB2'), P_RELAY.pin_pos('IN1'), via_x=224)
wires_text += elbow_wire(P_MCP.pin_pos('GPB3'), P_RELAY.pin_pos('IN2'), via_x=228)
wires_text += elbow_wire(P_MCP.pin_pos('GPB4'), P_RELAY.pin_pos('IN3'), via_x=232)
wires_text += elbow_wire(P_MCP.pin_pos('GPB5'), P_RELAY.pin_pos('IN4'), via_x=236)
wires_text += elbow_wire(P_MCP.pin_pos('GPB6'), P_RELAY.pin_pos('IN5'), via_x=240)

labels_text = ''
labels_text += power_stub(P_ESP.pin_pos('VBUS'), '+5V', 'left', shape='output')
labels_text += power_stub(P_ESP.pin_pos('V3V3'), '+3V3', 'right', shape='output')
labels_text += power_stub(P_ESP.pin_pos('GND'), 'GND', 'left', shape='input')

labels_text += power_stub(P_LS.pin_pos('LV'), '+3V3', 'down', shape='input')
labels_text += power_stub(P_LS.pin_pos('GND1'), 'GND', 'down', shape='input')
labels_text += power_stub(P_LS.pin_pos('HV'), '+5V', 'up', shape='input')
labels_text += power_stub(P_LS.pin_pos('GND2'), 'GND', 'up', shape='input')

labels_text += power_stub(P_MCP.pin_pos('VDD'), '+5V', 'left', shape='input')
labels_text += power_stub(P_MCP.pin_pos('VSS'), 'GND', 'left', shape='input')
labels_text += power_stub(P_MCP.pin_pos('RESET'), '+5V', 'right', shape='input')
labels_text += power_stub(P_MCP.pin_pos('A0'), 'GND', 'right', shape='input')
labels_text += power_stub(P_MCP.pin_pos('A1'), 'GND', 'right', shape='input')
labels_text += power_stub(P_MCP.pin_pos('A2'), 'GND', 'right', shape='input')

sch = f'''(kicad_sch
\t(version 20211123)
\t(generator eeschema)
\t(uuid {u()})
\t(paper "A2")
\t(lib_symbols
{lib_symbols_text}\t)
{instances_text}{wires_text}{labels_text}\t(sheet_instances
\t\t(path "/" (page "1"))
\t)
)
'''

with open('stromlaufplan.kicad_sch', 'w', encoding='utf-8') as f:
    f.write(sch)

# ---------------- standalone symbol library (so "Place Symbol" finds it too) ----------------
symlib = f'''(kicad_symbol_lib
\t(version 20211014)
\t(generator kicad_symbol_editor)
{lib_symbols_text}\t)
'''
with open('gartenwasser.kicad_sym', 'w', encoding='utf-8') as f:
    f.write(symlib)

with open('sym-lib-table', 'w', encoding='utf-8') as f:
    f.write(
        '(sym_lib_table\n'
        '\t(version 7)\n'
        '\t(lib (name "gartenwasser")(type "KiCad")(uri "${KIPRJMOD}/gartenwasser.kicad_sym")(options "")(descr "Gartenwasser Stromlaufplan Symbole"))\n'
        ')\n'
    )

import json as _json
project = {
    "board": {"design_settings": {}},
    "boards": [],
    "cvpcb": {},
    "erc": {"erc_exclusions": [], "meta": {"version": 0}, "pin_map": [], "rule_severities": {}},
    "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
    "meta": {"filename": "stromlaufplan.kicad_pro", "version": 1},
    "net_settings": {"classes": [{"name": "Default"}]},
    "pcbnew": {"page_layout_descr_file": ""},
    "schematic": {
        "drawing": {},
        "legacy_lib_dir": "",
        "legacy_lib_list": [],
    },
    "sheets": [],
    "text_variables": {},
}
with open('stromlaufplan.kicad_pro', 'w', encoding='utf-8') as f:
    _json.dump(project, f, indent=2)

print('written stromlaufplan.kicad_sch, gartenwasser.kicad_sym, sym-lib-table, stromlaufplan.kicad_pro')
