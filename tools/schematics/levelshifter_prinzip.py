import schemdraw
import schemdraw.elements as elm

schemdraw.config(fontsize=9, lw=1.3)

with schemdraw.Drawing(file='levelshifter_prinzip.svg', show=False) as d:
    d.config(unit=1.2)

    Q = elm.NMos(circle=False).up().label('Q1', loc='right')
    Rtop = elm.Resistor().up().at(Q.drain).label('R1\n10k', loc='right')
    dot_top = elm.Dot().at(Rtop.end)
    hv_line = elm.Line().up().length(0.6).at(dot_top.end)
    hv_pin = elm.Dot(open=True).at(hv_line.end).label('HV1', loc='top')

    Rbot = elm.Resistor().down().at(Q.source).label('R2\n10k', loc='right')
    dot_bot = elm.Dot().at(Rbot.end)
    lv_line = elm.Line().down().length(0.6).at(dot_bot.end)
    lv_pin = elm.Dot(open=True).at(lv_line.end).label('LV1', loc='bottom')

    gate_line = elm.Line().left().length(0.8).at(Q.gate)
    elm.Line().down().toy(dot_bot.start).at(gate_line.end)

print("ok")
