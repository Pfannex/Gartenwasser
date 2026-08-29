import schemdraw
import schemdraw.elements as elm
from schemdraw.elements import IcPin

schemdraw.config(fontsize=9, lw=1.3)

def _n(items):
    return [(name, pin, None) for name, pin in items]


esp_left = _n([
    ('VBUS', 'VBUS'), ('GND', 'GND'), ('GPIO16', 'UART0_TX'), ('GPIO17', 'UART0_RX'),
    ('RST', 'RST'), ('GPIO1', 'SPI_SCLK'), ('GPIO2', 'SPI_MOSI'), ('GPIO3', 'SPI_MISO'),
    ('GPIO4', ''), ('GPIO5', ''), ('GPIO6', ''),
])
esp_right = _n([
    ('VBAT', 'VBAT'), ('GND_2', 'GND'), ('GND_3', 'GND'), ('V3V3', '3V3'),
    ('GPIO19', 'I2C_SCL'), ('GPIO18', 'I2C_SDA'), ('GPIO13', 'USB_DP'), ('GPIO12', 'USB_DN'),
    ('GPIO9', ''), ('GPIO8', ''), ('GPIO7', ''),
])

mcp_left = _n([
    ('GPB0', '1'), ('GPB1', '2'), ('GPB2', '3'), ('GPB3', '4'),
    ('GPB4', '5'), ('GPB5', '6'), ('GPB6', '7'), ('GPB7', '8'),
    ('VDD', '9'), ('VSS', '10'), ('NC1', '11'), ('SCL', '12'), ('SDA', '13'), ('NC2', '14'),
])
mcp_right = _n([
    ('A0', '15'), ('A1', '16'), ('A2', '17'), ('RESET', '18'),
    ('INTB', '19'), ('INTA', '20'), ('GPA0', '21'), ('GPA1', '22'),
    ('GPA2', '23'), ('GPA3', '24'), ('GPA4', '25'), ('GPA5', '26'), ('GPA6', '27'), ('GPA7', '28'),
])

ls_left = [
    ('LV1', 'LV1', None), ('LV2', 'LV2', None), ('LV3', 'LV3', None), ('LV4', 'LV4', None),
    ('LV', None, 'LV_RAIL'), ('GND', None, 'LV_GND'),
]
ls_right = [
    ('HV1', 'HV1', None), ('HV2', 'HV2', None), ('HV3', 'HV3', None), ('HV4', 'HV4', None),
    ('HV', None, 'HV_RAIL'), ('GND', None, 'HV_GND'),
]

relay_left = [
    ('OUT0', 'V0', None), ('OUT1', 'V1', None), ('OUT2', 'V2', None),
    ('OUT3', 'V3', None), ('OUT4', 'V4', None), ('OUT5', 'V5', None),
]
relay_right = [
    ('IN0', None, None), ('IN1', None, None), ('IN2', None, None),
    ('IN3', None, None), ('IN4', None, None), ('IN5', None, None),
]


def mkpins(left, right):
    pins = []
    ntot = len(left)
    for i, (name, pin, anchorname) in enumerate(left):
        pins.append(IcPin(name=name, pin=pin if pin else None, side='L', slot=f'{ntot-i}/{ntot}',
                           anchorname=anchorname))
    for i, (name, pin, anchorname) in enumerate(right):
        pins.append(IcPin(name=name, pin=pin if pin else None, side='R', slot=f'{ntot-i}/{ntot}',
                           anchorname=anchorname))
    return pins


def vdd_flag(pin_anchor, direction, label):
    ext = elm.Line().at(pin_anchor).length(0.5)
    ext = ext.left() if direction == 'left' else (ext.right() if direction == 'right' else ext.up())
    elm.Vdd().at(ext.end).label(label, loc='top')


def gnd_flag(pin_anchor, direction):
    ext = elm.Line().at(pin_anchor).length(0.5)
    ext = ext.left() if direction == 'left' else (ext.right() if direction == 'right' else ext.down())
    elm.Ground().at(ext.end)


def routed_wire(start, end, midx):
    """3-segment route: horizontal to midx, vertical to target y, horizontal into target."""
    l1 = elm.Line().at(start).tox(midx)
    l2 = elm.Line().at(l1.end).toy(end[1])
    elm.Line().at(l2.end).to(end)


with schemdraw.Drawing(file='stromlaufplan.svg', show=False) as d:
    d.config(unit=1.3)

    esp = elm.Ic(pins=mkpins(esp_left, esp_right), size=(3.6, 11)).at((0, 0)).anchor('center')
    esp.label('Waveshare ESP32-C6-Touch-LCD-1.47', loc='top', ofst=0.5)

    mcp = elm.Ic(pins=mkpins(mcp_left, mcp_right), size=(3.6, 11)).at((22, 0)).anchor('center')
    mcp.label('MCP23017 (I2C-Adresse 0x20)', loc='top', ofst=0.5)

    relay = elm.Ic(pins=mkpins(relay_left, relay_right), size=(3.2, 6)).at((14, 3.3)).anchor('center')
    relay.label('Relaismodul (extern, 6 Kanaele)', loc='top', ofst=0.5)

    ls = elm.Ic(pins=mkpins(ls_left, ls_right), size=(3.0, 6)).at((11, -3.8)).anchor('center')
    ls.label('Logic-Level-Converter\n(bidirektional, MOSFET-basiert)', loc='bottom', ofst=0.5)

    # I2C wiring through the level shifter
    elm.Wire('-|').at(esp.GPIO19).to(ls.LV1)
    elm.Wire('-|').at(esp.GPIO18).to(ls.LV2)
    routed_wire(ls.HV1, mcp.SCL, midx=17.0)
    routed_wire(ls.HV2, mcp.SDA, midx=17.6)

    # valve relay wiring: GPB7=Hauptventil(V0), GPB2..GPB6=V1..V5
    elm.Wire('-|').at(mcp.GPB7).to(relay.IN0)
    elm.Wire('-|').at(mcp.GPB2).to(relay.IN1)
    elm.Wire('-|').at(mcp.GPB3).to(relay.IN2)
    elm.Wire('-|').at(mcp.GPB4).to(relay.IN3)
    elm.Wire('-|').at(mcp.GPB5).to(relay.IN4)
    elm.Wire('-|').at(mcp.GPB6).to(relay.IN5)

    # power rails
    vdd_flag(esp.VBUS, 'left', '5V')
    vdd_flag(esp.V3V3, 'right', '3V3')
    gnd_flag(esp.GND, 'left')

    vdd_flag(ls.LV_RAIL, 'left', '3V3')
    gnd_flag(ls.LV_GND, 'left')
    vdd_flag(ls.HV_RAIL, 'right', '5V')
    gnd_flag(ls.HV_GND, 'right')

    vdd_flag(mcp.VDD, 'left', '5V')
    gnd_flag(mcp.VSS, 'left')
    vdd_flag(mcp.RESET, 'right', '5V')
    gnd_flag(mcp.A0, 'right')
    gnd_flag(mcp.A1, 'right')
    gnd_flag(mcp.A2, 'right')

print("ok")
