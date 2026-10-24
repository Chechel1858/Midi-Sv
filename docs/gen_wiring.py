#!/usr/bin/env python3
"""Generates docs/wiring.html: separate wiring diagrams for the MIDI Analog
Filter alpha board.  usage: python3 docs/gen_wiring.py docs/wiring.html Board geometry is taken from the photo (board ~470x825
units, top-left origin, same orientation as the photo)."""
import sys

OUT = sys.argv[1]

# ---------------------------------------------------------------------------
# Board model


class Board:
    def __init__(self, uid, hl=(), show_proto=True):
        self.uid = uid
        self.hl = set(hl)
        self.show_proto = show_proto
        self.parts = []

    def h(self, key):
        return " hl" if key in self.hl else ""

    def add(self, s):
        self.parts.append(s)

    def svg(self):
        a = self.add
        a('<rect class="pcb" x="0" y="0" width="470" height="825" rx="10"/>')
        for x, y in ((14, 14), (456, 14), (14, 811), (456, 811)):
            a(f'<circle class="hole" cx="{x}" cy="{y}" r="6"/>')
        # divider between the filter and MIDI halves
        a('<line class="silkline" x1="8" y1="350" x2="462" y2="350"/>')
        a('<text class="silk" x="150" y="342">MIDI ANALOG FILTER</text>')
        a('<text class="silk" x="330" y="342">PRIBORE PROTOTYPE</text>')

        # ---- top half: analog filter ----
        for cx, cy in ((112, 98), (112, 300)):
            a(f'<circle class="cap" cx="{cx}" cy="{cy}" r="27"/>')
            a(f'<text class="silk tiny" x="{cx}" y="{cy + 4}" text-anchor="middle">220µ</text>')
        for key, x, y in (("trim", 142, 66), ("trim", 142, 272), ("trim", 186, 196)):
            a(f'<rect class="trimmer{self.h(key)}" x="{x}" y="{y}" width="22" height="22" rx="3"/>')
            a(f'<circle class="trimknob" cx="{x + 11}" cy="{y + 11}" r="6"/>')
        # transistor ladders and small parts (decor)
        for y in (34, 242):
            for i in range(5):
                a(f'<rect class="smd" x="{172 + i * 26}" y="{y}" width="16" height="10"/>')
                a(f'<rect class="ic" x="{172 + i * 26}" y="{y + 18}" width="14" height="14"/>')
        a('<rect class="ic" x="62" y="152" width="30" height="22"/>')
        a('<rect class="ic" x="384" y="152" width="30" height="22"/>')
        self.conn("lin", 5, 102, 38, 46, "v")
        self.conn("rin", 5, 150, 38, 46, "v")
        a('<text class="silk" x="50" y="128">L IN</text>')
        a('<text class="silk" x="50" y="176">R IN</text>')
        self.pad("rescvl", 100, 58)
        a('<text class="silk tiny" x="60" y="48">RES_L CV IN</text>')
        self.pad("rescvr", 100, 266)
        a('<text class="silk tiny" x="60" y="256">RES_R CV IN</text>')
        self.conn("freq", 241, 121, 44, 62, "v")
        a('<text class="silk" x="290" y="158">FILTER</text>')
        self.pad("fcvin", 205, 166)
        a('<text class="silk tiny" x="166" y="150">FILTER CV IN</text>')
        self.conn("resl", 373, 50, 42, 64, "v")
        a('<text class="silk tiny" x="372" y="44">RES L</text>')
        self.conn("resr", 373, 258, 42, 60, "v")
        a('<text class="silk tiny" x="372" y="252">RES R</text>')
        self.conn("lout", 428, 132, 40, 42, "v")
        self.conn("rout", 428, 176, 40, 42, "v")
        a('<text class="silk tiny" x="423" y="157" text-anchor="end">L OUT</text>')
        a('<text class="silk tiny" x="423" y="201" text-anchor="end">R OUT</text>')

        # ---- bottom half: power, MIDI, MCU ----
        for x, y in ((96, 362), (96, 416), (196, 372), (196, 418), (262, 362), (262, 410)):
            a(f'<rect class="ic" x="{x}" y="{y}" width="30" height="22"/>')
        for x, y in ((146, 372), (146, 418)):
            a(f'<rect class="ic" x="{x}" y="{y}" width="28" height="20"/>')
        a(f'<rect class="powerzone{self.h("power")}" x="88" y="356" width="236" height="96" rx="6"/>')
        a('<text class="silk tiny" x="206" y="402" text-anchor="middle" opacity=".75">питание ±15 В</text>')
        self.pad("vbus", 351, 421)
        a('<text class="silk tiny" x="336" y="412">+VBUS</text>')
        a(f'<rect class="sc" x="58" y="434" width="30" height="30" rx="3"/>')
        a(f'<rect class="sc" x="254" y="468" width="28" height="28" rx="3"/>')
        # USB-B (sticks out of the right edge)
        a(f'<rect class="metal{self.h("usb")}" x="408" y="418" width="86" height="60" rx="4"/>')
        a('<rect class="metalin" x="420" y="428" width="56" height="40" rx="3"/>')
        # filter CV block
        a('<rect class="silkbox" x="20" y="498" width="124" height="118" rx="4"/>')
        a('<text class="silk" x="26" y="514">FILTER CV</text>')
        a('<rect class="ic" x="92" y="526" width="26" height="34"/>')
        self.pad("j2", 64, 528)
        a('<text class="silk tiny" x="40" y="532">J2</text>')
        self.pad("cv", 64, 563)
        a('<text class="silk" x="56" y="596">CV</text>')
        # GATE
        self.pad("gate", 156, 489)
        a('<text class="silk" x="166" y="494">GATE</text>')
        a('<rect class="smd" x="174" y="502" width="12" height="9"/>')
        # USART1 header J22
        self.header("uart", 306, 506, 3)
        a('<text class="silk" x="338" y="520">USART1</text>')
        # NOTEON LED
        a(f'<rect class="led{self.h("led")}" x="406" y="512" width="16" height="10" rx="2"/>')
        a('<text class="silk tiny" x="428" y="521">NOTEON</text>')
        # DIN sockets (stick out to the right)
        self.din("thru", 385, 550, "THRU")
        self.din("min", 385, 664, "IN")
        # SWD header J13 (6 positions)
        self.header("swd", 201, 534, 6)
        a('<text class="silk" x="178" y="560" transform="rotate(-90 178 560)" text-anchor="end">SWD</text>')
        # MCU
        a(f'<rect class="mcu{self.h("mcu")}" x="276" y="584" width="56" height="56" rx="2"/>')
        for i in range(8):
            o = 590 + i * 6
            a(f'<line class="mcupin" x1="270" y1="{o}" x2="276" y2="{o}"/>')
            a(f'<line class="mcupin" x1="332" y1="{o}" x2="338" y2="{o}"/>')
            o2 = 282 + i * 6
            a(f'<line class="mcupin" x1="{o2}" y1="578" x2="{o2}" y2="584"/>')
            a(f'<line class="mcupin" x1="{o2}" y1="640" x2="{o2}" y2="646"/>')
        a('<text class="silk tiny" x="304" y="616" text-anchor="middle">STM32</text>')
        a('<text class="silk tiny" x="304" y="628" text-anchor="middle">F446</text>')
        a('<rect class="smd" x="280" y="664" width="14" height="10"/>')
        # test pads J25..J31
        for i, (x, y) in enumerate(TESTPADS):
            key = "clock" if i == 6 else "tp"
            self.pad(key, x, y, r=4.5)
        a('<text class="silk tiny" x="244" y="706">J31</text>')
        # LEARN button
        a(f'<rect class="button{self.h("learn")}" x="240" y="720" width="40" height="32" rx="3"/>')
        a('<circle class="buttoncap" cx="260" cy="736" r="9"/>')
        a('<text class="silk" x="260" y="772" text-anchor="middle">LEARN</text>')
        # MIDI input parts (decor)
        for x, y in ((340, 724), (340, 752), (368, 772)):
            a(f'<rect class="ic" x="{x}" y="{y}" width="22" height="16"/>')
        # prototyping area
        if self.show_proto:
            for r in range(14):
                for c in range(12):
                    x, y = 26 + c * 14, 632 + r * 13.5
                    if y > 805:
                        continue
                    a(f'<circle class="protohole" cx="{x}" cy="{y:.1f}" r="3.2"/>')
        return "\n".join(self.parts)

    def conn(self, key, x, y, w, h, orient):
        self.add(f'<rect class="jst{self.h(key)}" x="{x}" y="{y}" width="{w}" height="{h}" rx="3"/>')
        for i in range(3):
            py = y + h * (i + 1) / 4
            self.add(f'<rect class="jstpin" x="{x + w / 2 - 3}" y="{py - 3:.1f}" width="6" height="6"/>')

    def pad(self, key, x, y, r=5.5):
        self.add(f'<circle class="pad{self.h(key)}" cx="{x}" cy="{y}" r="{r}"/>')

    def header(self, key, x, y, n):
        self.add(f'<rect class="hdr{self.h(key)}" x="{x}" y="{y}" width="24" height="{n * 14 + 2}" rx="2"/>')
        for i in range(n):
            self.add(f'<rect class="hdrpin" x="{x + 8}" y="{y + 5 + i * 14}" width="8" height="8"/>')

    def din(self, key, x, y, label):
        self.add(f'<rect class="din{self.h(key)}" x="{x}" y="{y}" width="132" height="112" rx="6"/>')
        self.add(f'<circle class="dinhole" cx="{x + 58}" cy="{y + 56}" r="38"/>')
        for ang in (-90, -45, 0, 45, 90):
            import math
            px = x + 58 + 26 * math.cos(math.radians(ang + 180))
            py = y + 56 + 26 * math.sin(math.radians(ang + 180))
            # pins face the left side of the socket; drawn just for recognition
        for dx, dy in ((-24, 0), (-17, -17), (0, -24), (-17, 17), (0, 24)):
            self.add(f'<circle class="dinpin" cx="{x + 58 + dx}" cy="{y + 56 + dy}" r="3"/>')
        self.add(f'<text class="silk" x="{x + 98}" y="{y + 60}" text-anchor="middle">{label}</text>')


TESTPADS = [(205, 624), (207, 637), (210, 650), (214, 663), (219, 676), (226, 689), (233, 702)]
SWD_PINS = [(213, 543 + i * 14) for i in range(6)]
UART_PINS = [(318, 515 + i * 14) for i in range(3)]

# ---------------------------------------------------------------------------
# View helpers


class View:
    """Places a (cropped, scaled) board fragment into a figure."""

    def __init__(self, uid, w, h, region, at, scale, hl=(), proto=True):
        self.uid, self.w, self.h = uid, w, h
        self.rx0, self.ry0, self.rx1, self.ry1 = region
        self.ox, self.oy = at
        self.s = scale
        self.board = Board(uid, hl, proto)
        self.over = []

    def T(self, x, y):
        return (self.ox + self.s * (x - self.rx0), self.oy + self.s * (y - self.ry0))

    def add(self, s):
        self.over.append(s)

    def render(self, label):
        cw = (self.rx1 - self.rx0) * self.s
        ch = (self.ry1 - self.ry0) * self.s
        tx = self.ox - self.s * self.rx0
        ty = self.oy - self.s * self.ry0
        clip = f"clip-{self.uid}"
        full = self.rx0 <= 0 and self.ry0 <= 0 and self.rx1 >= 520 and self.ry1 >= 825
        parts = [
            f'<svg viewBox="0 0 {self.w} {self.h}" role="img" aria-label="{label}" class="diagram">',
            f'<defs><clipPath id="{clip}"><rect x="{self.ox}" y="{self.oy}" width="{cw:.1f}" height="{ch:.1f}" rx="12"/></clipPath>',
            f'<marker id="arr-{self.uid}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path class="arrowhead" d="M0,0 L10,5 L0,10 z"/></marker></defs>',
            f'<g clip-path="url(#{clip})"><g transform="translate({tx:.1f} {ty:.1f}) scale({self.s})">',
            self.board.svg(),
            "</g></g>",
        ]
        if not full:
            parts.append(
                f'<rect class="cropframe" x="{self.ox}" y="{self.oy}" width="{cw:.1f}" height="{ch:.1f}" rx="12"/>'
            )
        parts += self.over
        parts.append("</svg>")
        return "\n".join(parts)

    # drawing primitives in view coordinates ---------------------------------
    def wire(self, pts, cls, label=None, lpos=None, anchor="middle"):
        d = "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        self.add(f'<path class="wire {cls}" d="{d}"/>')
        x0, y0 = pts[0]
        x1, y1 = pts[-1]
        self.add(f'<circle class="wireend {cls}" cx="{x0:.1f}" cy="{y0:.1f}" r="4"/>')
        self.add(f'<circle class="wireend {cls}" cx="{x1:.1f}" cy="{y1:.1f}" r="4"/>')
        if label:
            lx, ly = lpos
            self.add(f'<text class="wlabel halo" x="{lx:.1f}" y="{ly:.1f}" text-anchor="{anchor}">{label}</text>')

    def leader(self, frm, to, text, anchor="start", sub=None):
        (x0, y0), (x1, y1) = frm, to
        self.add(f'<line class="leader" x1="{x0:.1f}" y1="{y0:.1f}" x2="{x1:.1f}" y2="{y1:.1f}"/>')
        self.add(f'<circle class="leaderdot" cx="{x1:.1f}" cy="{y1:.1f}" r="3"/>')
        dx = -6 if anchor == "end" else 6
        self.add(f'<text class="lbl halo" x="{x0 + dx:.1f}" y="{y0 + 5:.1f}" text-anchor="{anchor}">{text}</text>')
        if sub:
            self.add(
                f'<text class="lblsub halo" x="{x0 + dx:.1f}" y="{y0 + 22:.1f}" text-anchor="{anchor}">{sub}</text>'
            )

    def text(self, x, y, s, cls="lbl", anchor="start"):
        self.add(f'<text class="{cls}" x="{x:.1f}" y="{y:.1f}" text-anchor="{anchor}">{s}</text>')

    def box(self, x, y, w, h, title, lines=(), cls="dev"):
        self.add(f'<rect class="{cls}" x="{x}" y="{y}" width="{w}" height="{h}" rx="10"/>')
        self.add(f'<text class="devtitle" x="{x + 14}" y="{y + 26}">{title}</text>')
        for i, ln in enumerate(lines):
            self.add(f'<text class="devline" x="{x + 14}" y="{y + 50 + i * 20}">{ln}</text>')


# ---------------------------------------------------------------------------
# Figures

figs = {}

# 1. Overview ----------------------------------------------------------------
v = View("obzor", 1000, 880, (0, 0, 530, 830), (250, 25), 1.0,
         hl=("lin", "rin", "lout", "rout", "freq", "resl", "resr", "fcvin", "usb", "swd", "uart",
             "thru", "min", "learn", "cv", "gate", "clock", "led", "power"))
L = 240  # left label column x (anchor end)
R = 790
v.leader((L, 85), v.T(100, 58), "RES_L CV IN", "end", "площадка, CV резонанса L")
v.leader((L, 150), v.T(24, 125), "Аудио вход L", "end", "белый разъём J6")
v.leader((L, 210), v.T(24, 173), "Аудио вход R", "end", "белый разъём J12")
v.leader((L, 265), v.T(205, 166), "FILTER CV IN (J7)", "end", "внешний CV на срез")
v.leader((L, 320), v.T(100, 266), "RES_R CV IN", "end", "площадка, CV резонанса R")
v.leader((L, 425), v.T(96, 404), "Питание ±15 В", "end", "делается из USB, само")
v.leader((L, 515), v.T(156, 489), "GATE (J14)", "end", "площадка, 0 / 5 В")
v.leader((L, 588), v.T(64, 563), "CV (J1)", "end", "площадка, 0…10 В")
v.leader((L, 665), v.T(213, 575), "SWD (J13)", "end", "сюда ST-Link")
v.leader((L, 715), v.T(233, 702), "CLOCK (J31)", "end", "площадка, такт")
v.leader((L, 770), v.T(240, 736), "Кнопка LEARN", "end")
v.leader((L, 825), v.T(110, 790), "Макетное поле", "end", "для своих доработок")
v.leader((R, 75), v.T(394, 82), "RES L", "start", "потенциометр резонанса L")
v.leader((R, 125), v.T(263, 152), "FILTER", "start", "потенциометр частоты среза")
v.leader((R, 175), v.T(448, 153), "Аудио выход L", "start", "разъём J4")
v.leader((R, 230), v.T(448, 197), "Аудио выход R", "start", "разъём J10")
v.leader((R, 315), v.T(394, 288), "RES R", "start", "потенциометр резонанса R")
v.leader((R, 450), v.T(494, 448), "USB-B", "start", "питание + MIDI + COM-порт")
v.leader((R, 505), v.T(414, 517), "Светодиод NOTEON", "start")
v.leader((R, 560), v.T(330, 529), "USART1 (J22)", "start", "запасная консоль")
v.leader((R, 640), v.T(517, 606), "MIDI THRU", "start", "верхний DIN")
v.leader((R, 760), v.T(517, 720), "MIDI IN", "start", "нижний DIN")
figs["obzor"] = v.render("Вид сверху на плату: где какие разъёмы, площадки и кнопка")

# 2. First flash with ST-Link -------------------------------------------------
v = View("proshivka", 980, 480, (150, 400, 500, 680), (300, 40), 1.45, hl=("swd", "usb"), proto=False)
# ST-Link dongle on the left
sx, sy = 30, 150
v.add(f'<rect class="stlink" x="{sx}" y="{sy}" width="190" height="250" rx="14"/>')
v.add(f'<rect class="dev" x="{sx + 60}" y="{sy - 50}" width="70" height="54" rx="4"/>')  # USB plug
v.text(sx + 95, sy - 60, "в USB компьютера", "lblsub", "middle")
v.text(sx + 95, sy + 34, "ST-LINK V2", "devtitle", "middle")
v.text(sx + 95, sy + 54, "надписи на корпусе", "devline", "middle")
st_pins = [("3.3V", "red"), ("SWCLK", "blue"), ("GND", "gnd"), ("SWDIO", "amber")]
st_xy = {}
for i, (name, cls) in enumerate(st_pins):
    py = sy + 92 + i * 38
    v.add(f'<rect class="hdrpin big" x="{sx + 150}" y="{py - 9}" width="18" height="18"/>')
    v.text(sx + 138, py + 5, name, "pinname", "end")
    st_xy[name] = (sx + 168, py)
swd = [v.T(x, y) for x, y in SWD_PINS]
right_edge = v.T(232, 0)[0]
# wires: ST-Link -> J13 pins 2, 3, 4
for name, cls, pin, lane in (("SWCLK", "w-blue", 1, 245), ("GND", "w-gnd", 2, 262), ("SWDIO", "w-amber", 3, 279)):
    x0, y0 = st_xy[name]
    px, py = swd[pin]
    v.wire([(x0, y0), (lane, y0), (lane, py), (px, py)], cls)
x0, y0 = st_xy["3.3V"]
v.add(f'<line class="wire w-red dashed" x1="{x0}" y1="{y0}" x2="{x0 + 40}" y2="{y0}"/>')
v.text(x0 + 46, y0 - 8, "не подключать", "warn")
# pin numbers next to J13
for i, (px, py) in enumerate(swd):
    v.text(right_edge + 8, py + 5, f"{i + 1}", "pinnum halo")
v.leader((610, 24), v.T(213, 541), "пин 1: 3.3 В при включённом USB", "start")
# PC / USB power
ux, uy = v.T(494, 448)
v.box(830, 190, 140, 100, "Компьютер", ["питание платы", "по USB"])
v.wire([(ux, uy), (900, uy), (900, 190)], "w-usb", "USB-кабель", (905, 100), "start")
figs["proshivka"] = v.render("ST-Link подключается к разъёму SWD (J13) тремя проводами, плата питается от USB")

# 3. USB: normal start vs updater ---------------------------------------------
def usb_panel(uid, title, hold, devices, led):
    v = View(uid, 560, 470, (225, 405, 500, 790), (20, 60), 0.92, hl=("usb", "learn", "led"), proto=False)
    v.text(20, 34, title, "paneltitle")
    ux, uy = v.T(494, 448)
    v.box(330, 120, 210, 150, "Компьютер видит", devices)
    v.wire([(ux, uy), (ux + 20, uy), (ux + 20, 195), (330, 195)], "w-usb")
    lx, ly = v.T(260, 736)
    if hold:
        v.add(f'<circle class="press" cx="{lx:.1f}" cy="{ly:.1f}" r="22"/>')
        v.leader((330, 400), (lx + 22, ly), "держать, пока втыкаешь USB", "start")
    else:
        v.leader((330, 400), (lx + 12, ly), "не нажимать", "start")
    ex, ey = v.T(414, 517)
    v.leader((330, 330), (ex + 9, ey), led, "start")
    return v.render(title)

figs["usb_a"] = usb_panel("usba", "Обычное включение", False,
                          ["MIDI: «MIDI-CV Alpha»", "COM-порт: консоль", "(драйверы не нужны)"],
                          "NOTEON: горит на ноте")
figs["usb_b"] = usb_panel("usbb", "Режим обновления", True,
                          ["«MIDI-CV Alpha Updater»", "MIDI и COM нет", "→ dfu-util -a 0 -D …"],
                          "NOTEON медленно мигает")

# 4. MIDI ---------------------------------------------------------------------
v = View("midi", 900, 470, (300, 520, 530, 800), (40, 40), 1.35, hl=("thru", "min"), proto=False)
tx_, ty_ = v.T(517, 606)
ix, iy = v.T(517, 720)
# keyboard
kx, ky = 560, 300
v.add(f'<rect class="dev" x="{kx}" y="{ky}" width="310" height="120" rx="10"/>')
for i in range(14):
    v.add(f'<rect class="key" x="{kx + 15 + i * 20}" y="{ky + 50}" width="18" height="58" rx="2"/>')
for i in (0, 1, 3, 4, 5, 7, 8, 10, 11, 12):
    v.add(f'<rect class="keyb" x="{kx + 28 + i * 20}" y="{ky + 50}" width="12" height="34" rx="1"/>')
v.text(kx + 14, ky + 30, "MIDI-клавиатура", "devtitle")
v.wire([(ix, iy), (kx - 30, iy), (kx - 30, ky + 80), (kx, ky + 80)], "w-midi", "MIDI OUT клавиатуры → IN", (720, 290))
# second synth
v.box(560, 60, 310, 110, "Другой синт (если нужен)", ["получает копию входа", "даже без прошивки"])
v.wire([(tx_, ty_), (kx - 60, ty_), (kx - 60, 115), (560, 115)], "w-thru", "THRU → MIDI IN синта", (720, 200))
v.leader((60, 30), v.T(440, 606), "верхний — THRU", "start")
v.leader((60, 455), v.T(440, 720), "нижний — IN", "start")
figs["midi"] = v.render("MIDI-клавиатура подключается в нижний DIN (IN), верхний DIN (THRU) отдаёт копию дальше")

# 5. Outputs ------------------------------------------------------------------
v = View("vyhody", 900, 450, (10, 470, 260, 720), (40, 40), 1.4, hl=("cv", "gate", "clock", "swd"), proto=False)
cvx, cvy = v.T(64, 563)
gx, gy = v.T(156, 489)
clx, cly = v.T(233, 702)
swd = [v.T(x, y) for x, y in SWD_PINS]
mx = 640
v.add(f'<rect class="dev" x="{mx}" y="110" width="230" height="310" rx="10"/>')
v.text(mx + 16, 140, "Синтезатор / модуль", "devtitle")
jacks = [("CV / 1V/oct", 180, "w-cv"), ("GATE", 240, "w-gate"), ("GND (земля)", 300, "w-gnd"), ("CLOCK", 380, "w-clock")]
jx = {}
for name, y, cls in jacks:
    v.add(f'<circle class="jack" cx="{mx + 40}" cy="{y}" r="13"/>')
    v.add(f'<circle class="jackin" cx="{mx + 40}" cy="{y}" r="5"/>')
    v.text(mx + 62, y + 5, name, "pinname")
    jx[name] = (mx + 27, y)
v.wire([(cvx, cvy), (78, cvy), (78, 22), (580, 22), (580, 180), jx["CV / 1V/oct"]], "w-cv", "CV: 0…10 В", (330, 16))
v.wire([(gx, gy), (gx, 50), (550, 50), (550, 240), jx["GATE"]], "w-gate", "GATE: 0 / 5 В", (400, 44))
gpx, gpy = swd[2]
v.wire([(gpx, gpy), (510, gpy), (510, 300), jx["GND (земля)"]], "w-gnd", "SWD пин 3 = GND", (455, gpy - 8))
v.wire([(clx, cly), (clx, 420), (600, 420), (600, 380), jx["CLOCK"]], "w-clock", "CLOCK: 3.3 В, импульс вниз", (470, 440))
figs["vyhody"] = v.render("CV, GATE и CLOCK — площадки на плате; землю берём с пина 3 разъёма SWD")

# 6. Filter half ---------------------------------------------------------------
v = View("filtr", 1110, 620, (0, 0, 475, 352), (260, 70), 1.2,
         hl=("lin", "rin", "lout", "rout", "freq", "resl", "resr", "fcvin", "rescvl", "rescvr", "trim"))
a_l = v.T(5, 125)
a_r = v.T(5, 173)
v.box(20, 200, 180, 100, "Источник звука", ["линейный уровень", "L и R"])
v.wire([(200, 232), (232, 232), (232, a_l[1]), a_l], "w-audio")
v.wire([(200, 268), (244, 268), (244, a_r[1]), a_r], "w-audio")
ol = v.T(468, 153)
orr = v.T(468, 197)
v.box(900, 235, 180, 100, "Микшер", ["вход L и R"])
v.wire([ol, (870, ol[1]), (870, 268), (900, 268)], "w-audio")
v.wire([orr, (884, orr[1]), (884, 300), (900, 300)], "w-audio")
# pots
fp = v.T(285, 152)
v.add('<circle class="pot" cx="640" cy="560" r="24"/><line class="potline" x1="640" y1="560" x2="652" y2="541"/>')
v.text(680, 556, "FILTER: потенциометр 10 кОм (B10K)", "lbl")
v.text(680, 576, "средний вывод — на средний пин разъёма", "lblsub")
v.wire([fp, (640, fp[1]), (640, 536)], "w-pot")
rl = v.T(415, 82)
rr = v.T(415, 288)
for (px, py), (cx, cy), name in ((rl, (990, 120), "RES L: 10 кОм"), (rr, (990, 470), "RES R: 10 кОм")):
    v.add(f'<circle class="pot" cx="{cx}" cy="{cy}" r="22"/><line class="potline" x1="{cx}" y1="{cy}" x2="{cx + 10}" y2="{cy - 17}"/>')
    v.wire([(px, py), (950, py), (950, cy), (cx - 22, cy)], "w-pot")
    v.text(cx, cy + 44, name, "lblsub", "middle")
v.leader((240, 140), v.T(100, 58), "RES_L CV IN", "end", "CV резонанса L")
v.leader((240, 360), v.T(205, 166), "FILTER CV IN", "end", "внешний CV на срез")
v.leader((240, 420), v.T(100, 266), "RES_R CV IN", "end", "CV резонанса R")
v.leader((600, 34), v.T(153, 77), "подстроечники — не трогать без нужды", "start")
figs["filtr"] = v.render("Аудио входит слева, выходит справа; ручки частоты и резонанса — на белые разъёмы")

# 7. LEARN timeline (no board) -------------------------------------------------
def learn_fig():
    W, H = 920, 330
    x0, x1 = 60, 880
    sec = lambda t: x0 + (x1 - x0) * t / 16.0
    out = [f'<svg viewBox="0 0 {W} {H}" role="img" aria-label="Что делает кнопка LEARN в зависимости от времени удержания" class="diagram">']
    out.append(f'<line class="axis" x1="{x0}" y1="200" x2="{x1}" y2="200"/>')
    for t in range(0, 17):
        x = sec(t)
        out.append(f'<line class="tick" x1="{x:.1f}" y1="200" x2="{x:.1f}" y2="{208 if t % 5 else 214}"/>')
        if t in (0, 1, 3, 8, 15):
            out.append(f'<text class="ticklbl" x="{x:.1f}" y="232" text-anchor="middle">{t} с</text>')
    zones = [
        (0, 1, "z1", "коротко", "обучение CC", "фильтр ← крутанутая ручка"),
        (3, 8, "z2", "3 с", "обучение канала", "нажми ноту на нужном канале"),
        (8, 15, "z3", "8 с", "Omni", "слушать все каналы"),
        (15, 16, "z4", "15 с", "заводские", "сброс настроек"),
    ]
    for a_, b_, cls, t1, t2, t3 in zones:
        xa, xb = sec(a_), sec(b_)
        out.append(f'<rect class="zone {cls}" x="{xa:.1f}" y="168" width="{xb - xa:.1f}" height="32" rx="4"/>')
    labels = [(0, 0, "отпустить до 1 с:", "обучение CC для фильтра", "ещё нажатие — для гнезда CV"),
              (3, 1, "держать 3 с:", "обучение MIDI-канала", "нажми ноту на клавиатуре"),
              (8, 0, "держать 8 с:", "Omni — все каналы", ""),
              (15, 1, "15 с:", "заводские настройки", "")]
    for t, row, a1, a2, a3 in labels:
        x = sec(t) + 6
        anchor = "start"
        if t == 15:
            x = sec(16)
            anchor = "end"
        y = 40 if row == 0 else 108
        out.append(f'<line class="zonemark" x1="{sec(t):.1f}" y1="{y - 14}" x2="{sec(t):.1f}" y2="168"/>')
        out.append(f'<text class="lblsub" x="{x:.1f}" y="{y}" text-anchor="{anchor}">{a1}</text>')
        out.append(f'<text class="lbl" x="{x:.1f}" y="{y + 22}" text-anchor="{anchor}">{a2}</text>')
        if a3:
            out.append(f'<text class="lblsub" x="{x:.1f}" y="{y + 42}" text-anchor="{anchor}">{a3}</text>')
    out.append(f'<text class="lblsub" x="{x0}" y="280">Держать кнопку, втыкая USB → режим обновления прошивки (это отдельно, до запуска).</text>')
    out.append(f'<text class="lblsub" x="{x0}" y="304">Обучение само отменяется через 10 с; всё выученное сразу сохраняется.</text>')
    out.append("</svg>")
    return "\n".join(out)

figs["learn"] = learn_fig()

def led_fig():
    W, H = 920, 250
    out = [f'<svg viewBox="0 0 {W} {H}" role="img" aria-label="Как мигает светодиод NOTEON в разных режимах" class="diagram">']
    rows = [
        ("Нота нажата", [(0, 600)]),
        ("Обучение CC (фильтр)", [(i * 200, i * 200 + 100) for i in range(3)]),
        ("Обучение CC (гнездо CV)", [(0, 75), (150, 225), (600, 675)]),
        ("Обучение канала", [(0, 500)]),
        ("Режим обновления", [(0, 500)]),
    ]
    x0, x1 = 300, 880
    sc = (x1 - x0) / 1000.0
    for r, (name, ons) in enumerate(rows):
        y = 40 + r * 42
        out.append(f'<text class="lbl" x="{x0 - 20}" y="{y + 5}" text-anchor="end">{name}</text>')
        out.append(f'<line class="axis faint" x1="{x0}" y1="{y + 10}" x2="{x1}" y2="{y + 10}"/>')
        for a_, b_ in ons:
            out.append(f'<rect class="ledon" x="{x0 + a_ * sc:.1f}" y="{y - 8}" width="{(b_ - a_) * sc:.1f}" height="18" rx="3"/>')
    out.append(f'<text class="ticklbl" x="{x0}" y="{H - 8}">0</text>')
    out.append(f'<text class="ticklbl" x="{x1}" y="{H - 8}" text-anchor="end">1 секунда</text>')
    out.append("</svg>")
    return "\n".join(out)

figs["led"] = led_fig()

# 8. USART1 -------------------------------------------------------------------
v = View("uart", 860, 330, (250, 470, 470, 600), (360, 40), 2.1, hl=("uart",), proto=False)
up = [v.T(x, y) for x, y in UART_PINS]
v.add('<rect class="dev" x="20" y="70" width="270" height="190" rx="10"/>')
v.text(34, 98, "USB-UART 3.3 В", "devtitle")
v.text(34, 120, "CH340, CP2102, FT232…", "devline")
for (px, py), cls, txt in zip(up, ("w-blue", "w-amber", "w-gnd"), ("RX ← пин 1 (TX)", "TX → пин 2 (RX)", "GND ← пин 3")):
    v.text(34, py + 5, txt, "pinname")
    v.wire([(290, py), (px, py)], cls)
v.text(360, 318, "Пин 1 — верхний. Порядок проверь прозвонкой: GND звонится с пином 3 SWD.", "lblsub")
figs["uart"] = v.render("Запасная консоль: USB-UART переходник на 3.3 В к разъёму USART1, TX и RX крест-накрест")

# ---------------------------------------------------------------------------
# Page

SECTIONS = [
    ("obzor", "Обзор платы", None),
    ("proshivka", "Первая прошивка", None),
    ("usb", "USB и обновление", None),
    ("midi", "MIDI", None),
    ("vyhody", "CV, GATE, CLOCK", None),
    ("filtr", "Фильтр и аудио", None),
    ("learn", "Кнопка и светодиод", None),
    ("uart", "Запасная консоль", None),
]

CSS = r"""
/* Layout: a fitter's sheet — sticky chip nav, then one section per job: diagram first, steps beside it */
:root {
  --bg: #f3f5f2; --fg: #17201b; --muted: #56635b; --line: #c9d2cb; --card: #ffffff;
  --accent: #e8a317; --accent-soft: #e8a31733;
  --pcb: #24452f; --pcb-edge: #17301f; --silk: #e8efe6; --plastic: #f1efe7; --black-part: #161a18;
  --metal: #c3c9cc; --pad: #d8d2bf; --stlink: #3f9a5e33;
  --w-blue: #2f6fdb; --w-amber: #d48806; --w-red: #d23f3f; --w-teal: #128a78; --w-violet: #7a52c7;
  --w-usb: #56635b; --w-midi: #2f6fdb; --w-audio: #128a78; --w-pot: #7a52c7;
  --z1: #2f6fdb; --z2: #128a78; --z3: #7a52c7; --z4: #d23f3f;
  --f-display: "Unbounded", "Golos Text", system-ui, sans-serif;
  --f-body: "Golos Text", system-ui, -apple-system, "Segoe UI", sans-serif;
  --f-mono: "JetBrains Mono", ui-monospace, "SFMono-Regular", Menlo, Consolas, monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #111613; --fg: #e4ebe5; --muted: #9aa89f; --line: #2c3730; --card: #1a211c;
  --accent: #f2b33a; --accent-soft: #f2b33a33; --pcb: #29513a; --pcb-edge: #3b6a4d; --stlink: #3f9a5e44;
  --w-blue: #6b9bf0; --w-amber: #f0a92a; --w-red: #ef6b6b; --w-teal: #3cc4ac; --w-violet: #a98af0;
  --w-usb: #9aa89f; --w-midi: #6b9bf0; --w-audio: #3cc4ac; --w-pot: #a98af0;
  --z1: #6b9bf0; --z2: #3cc4ac; --z3: #a98af0; --z4: #ef6b6b; color-scheme: dark; } }
:root[data-theme="dark"] {
  --bg: #111613; --fg: #e4ebe5; --muted: #9aa89f; --line: #2c3730; --card: #1a211c;
  --accent: #f2b33a; --accent-soft: #f2b33a33; --pcb: #29513a; --pcb-edge: #3b6a4d; --stlink: #3f9a5e44;
  --w-blue: #6b9bf0; --w-amber: #f0a92a; --w-red: #ef6b6b; --w-teal: #3cc4ac; --w-violet: #a98af0;
  --w-usb: #9aa89f; --w-midi: #6b9bf0; --w-audio: #3cc4ac; --w-pot: #a98af0;
  --z1: #6b9bf0; --z2: #3cc4ac; --z3: #a98af0; --z4: #ef6b6b; color-scheme: dark; }

body { margin: 0; background: var(--bg); color: var(--fg); font: 16px/1.55 var(--f-body);
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); }
body::before { content: ""; position: fixed; inset: 0 0 auto; height: env(safe-area-inset-top, 0px); z-index: 10; background: var(--bg); }
.wrap { max-width: 1180px; margin: 0 auto; padding-block: 28px 64px; }
header.top h1 { font: 600 clamp(24px, 4vw, 36px)/1.15 var(--f-display); margin: 0 0 10px; text-wrap: balance; letter-spacing: -0.01em; }
header.top p { margin: 0; color: var(--muted); max-width: 68ch; }
nav.chips { position: sticky; top: env(safe-area-inset-top, 0px); z-index: 5; background: var(--bg);
  display: flex; flex-wrap: wrap; gap: 8px; padding-block: 14px; margin-top: 18px; border-bottom: 1px solid var(--line); }
nav.chips a { font: 500 13px/1 var(--f-body); color: var(--fg); text-decoration: none; padding: 8px 12px;
  border: 1px solid var(--line); border-radius: 999px; background: var(--card); }
nav.chips a:hover, nav.chips a:focus-visible { border-color: var(--accent); outline: none; box-shadow: 0 0 0 3px var(--accent-soft); }
section.job { padding-block: 40px 8px; border-bottom: 1px solid var(--line); scroll-margin-top: 70px; }
section.job h2 { font: 600 22px/1.2 var(--f-display); margin: 0 0 6px; text-wrap: balance; }
section.job .lede { color: var(--muted); margin: 0 0 18px; max-width: 70ch; }
.grid { display: grid; grid-template-columns: minmax(0, 1fr) minmax(260px, 340px); gap: 28px; align-items: start; }
.grid.wide { grid-template-columns: minmax(0, 1fr); }
@media (max-width: 860px) { .grid { grid-template-columns: minmax(0, 1fr); } }
.pair { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 18px; }
@media (max-width: 860px) { .pair { grid-template-columns: minmax(0, 1fr); } }
figure { margin: 0; min-width: 0; }
.scroll { overflow-x: auto; border: 1px solid var(--line); border-radius: 12px; background: var(--card); }
figcaption { font-size: 13px; color: var(--muted); margin-top: 8px; }
.steps { min-width: 0; }
.steps ol, .steps ul { margin: 0; padding-left: 22px; display: grid; gap: 8px; }
.steps h3 { font: 600 12px/1 var(--f-body); text-transform: uppercase; letter-spacing: .08em; color: var(--muted); margin: 18px 0 10px; }
.steps h3:first-child { margin-top: 0; }
code, .mono { font-family: var(--f-mono); font-size: .9em; }
code { background: var(--accent-soft); padding: 1px 5px; border-radius: 4px; }
table.pins { border-collapse: collapse; width: 100%; font-size: 14px; font-variant-numeric: tabular-nums; }
table.pins td, table.pins th { border-bottom: 1px solid var(--line); padding: 6px 8px; text-align: left; }
table.pins th { font-weight: 600; color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: .06em; }
.note { border-left: 3px solid var(--w-red); padding: 8px 12px; background: var(--card); border-radius: 0 8px 8px 0; font-size: 14px; }
.swatch { display: inline-block; width: 22px; height: 4px; border-radius: 2px; vertical-align: middle; margin-right: 6px; }

/* ---- SVG ---- */
svg.diagram { display: block; width: 100%; min-width: 560px; height: auto; font-family: var(--f-body); }
.pcb { fill: var(--pcb); stroke: var(--pcb-edge); stroke-width: 2; }
.hole { fill: var(--bg); stroke: var(--pad); stroke-width: 2; }
.silk { fill: var(--silk); font: 600 11px var(--f-mono); letter-spacing: .04em; }
.silk.tiny { font-size: 8.5px; }
.silkline, .silkbox { stroke: var(--silk); stroke-width: 1.5; fill: none; opacity: .7; }
.cap { fill: #2a2d2c; stroke: var(--silk); stroke-width: 1; }
.trimmer { fill: var(--plastic); stroke: #0004; }
.trimknob { fill: #d9d6cc; stroke: #0005; }
.smd { fill: #3a3f3c; }
.ic, .mcu { fill: var(--black-part); }
.mcupin { stroke: var(--metal); stroke-width: 1.5; }
.jst { fill: var(--plastic); stroke: #0004; }
.jstpin { fill: #bdb7a6; }
.pad { fill: var(--pad); stroke: #0003; }
.hdr { fill: var(--black-part); }
.hdrpin { fill: #e3b341; }
.hdrpin.big { stroke: #0005; }
.metal { fill: var(--metal); stroke: #0004; }
.metalin { fill: #8d9599; }
.din { fill: var(--black-part); }
.dinhole { fill: #2b302d; stroke: #4b524d; }
.dinpin { fill: #9aa19c; }
.sc { fill: var(--pad); opacity: .85; }
.led { fill: #f5f5ef; stroke: #0004; }
.button { fill: #2a2d2c; }
.buttoncap { fill: #efefe9; }
.protohole { fill: none; stroke: var(--pad); stroke-width: 1.4; opacity: .8; }
.powerzone { fill: none; stroke: none; }
.hl { stroke: var(--accent); stroke-width: 3.5; paint-order: stroke; }
.powerzone.hl { stroke-dasharray: 6 4; stroke-width: 2.5; }
.cropframe { fill: none; stroke: var(--line); stroke-width: 1.5; stroke-dasharray: 5 5; }

.lbl { fill: var(--fg); font-size: 15px; font-weight: 500; }
.lblsub { fill: var(--muted); font-size: 13px; }
.halo { paint-order: stroke; stroke: var(--card); stroke-width: 5px; stroke-linejoin: round; }
.leader { stroke: var(--muted); stroke-width: 1.2; }
.leaderdot { fill: var(--accent); }
.wire { fill: none; stroke-width: 3.5; stroke-linejoin: round; stroke-linecap: round; }
.wire.dashed { stroke-dasharray: 6 5; }
.wireend { stroke: none; }
.wlabel { fill: var(--fg); font-size: 13px; font-weight: 600; }
.w-blue { stroke: var(--w-blue); } .wireend.w-blue { fill: var(--w-blue); }
.w-amber { stroke: var(--w-amber); } .wireend.w-amber { fill: var(--w-amber); }
.w-red { stroke: var(--w-red); }
.w-gnd { stroke: var(--fg); } .wireend.w-gnd { fill: var(--fg); }
.w-usb { stroke: var(--w-usb); } .wireend.w-usb { fill: var(--w-usb); }
.w-midi { stroke: var(--w-midi); } .wireend.w-midi { fill: var(--w-midi); }
.w-thru { stroke: var(--w-violet); } .wireend.w-thru { fill: var(--w-violet); }
.w-cv { stroke: var(--w-amber); } .wireend.w-cv { fill: var(--w-amber); }
.w-gate { stroke: var(--w-teal); } .wireend.w-gate { fill: var(--w-teal); }
.w-clock { stroke: var(--w-violet); } .wireend.w-clock { fill: var(--w-violet); }
.w-audio { stroke: var(--w-audio); } .wireend.w-audio { fill: var(--w-audio); }
.w-pot { stroke: var(--w-pot); } .wireend.w-pot { fill: var(--w-pot); }
.wireend { stroke: none; }
.dev { fill: var(--card); stroke: var(--fg); stroke-width: 1.5; }
.stlink { fill: var(--stlink); stroke: var(--fg); stroke-width: 1.5; }
.devtitle { fill: var(--fg); font-size: 15px; font-weight: 600; }
.devline { fill: var(--muted); font-size: 13px; }
.pinname { fill: var(--fg); font: 600 13px var(--f-mono); }
.pinnum { fill: var(--muted); font: 600 12px var(--f-mono); }
.warn { fill: var(--w-red); font-size: 13px; font-weight: 600; }
.paneltitle { fill: var(--fg); font: 600 17px var(--f-display); }
.press { fill: var(--accent-soft); stroke: var(--accent); stroke-width: 3; }
.key { fill: var(--card); stroke: var(--fg); stroke-width: 1; }
.keyb { fill: var(--fg); }
.jack { fill: var(--card); stroke: var(--fg); stroke-width: 2; }
.jackin { fill: var(--fg); }
.pot { fill: var(--card); stroke: var(--fg); stroke-width: 2; }
.potline { stroke: var(--fg); stroke-width: 2.5; stroke-linecap: round; }
.arrowhead { fill: var(--fg); }
.axis { stroke: var(--fg); stroke-width: 1.5; }
.axis.faint { stroke: var(--line); }
.tick { stroke: var(--fg); stroke-width: 1.2; }
.ticklbl { fill: var(--muted); font: 500 12px var(--f-mono); }
.zone { opacity: .9; } .zone.z1 { fill: var(--z1); } .zone.z2 { fill: var(--z2); } .zone.z3 { fill: var(--z3); } .zone.z4 { fill: var(--z4); }
.zonemark { stroke: var(--line); stroke-width: 1; stroke-dasharray: 3 3; }
.ledon { fill: var(--accent); }
@media (prefers-reduced-motion: no-preference) { nav.chips a { transition: border-color .15s, box-shadow .15s; } }
"""


def fig(key, caption):
    return f'<figure><div class="scroll">{figs[key]}</div><figcaption>{caption}</figcaption></figure>'


body = []
body.append('''<header class="top">
<h1>Плата MIDI Analog Filter: куда что подключать</h1>
<p>Схемы нарисованы по фото платы, в той же ориентации: фильтр сверху, MIDI и процессор снизу,
DIN-разъёмы и USB справа. На каждой схеме жёлтым подсвечено только то, что нужно для этого шага.
Подробности и команды — в README репозитория.</p>
</header>''')
body.append('<nav class="chips" aria-label="Схемы">' + "".join(f'<a href="#{k}">{t}</a>' for k, t, _ in SECTIONS) + "</nav>")

body.append(f'''<section class="job" id="obzor"><h2>Обзор платы</h2>
<p class="lede">Три зоны: сверху аналоговый фильтр (стерео, L и R), посередине питание (из USB делаются ±15 В),
снизу MIDI, процессор STM32F446 и выходы управляющих напряжений.</p>
<div class="grid wide">{fig("obzor", "Все разъёмы, площадки и кнопка. CV, GATE и CLOCK на этой плате — круглые площадки под пайку, а не гнёзда.")}</div>
</section>''')

body.append(f'''<section class="job" id="proshivka"><h2>Первая прошивка через ST-Link</h2>
<p class="lede">Нужна один раз: заливает загрузчик и прошивку вместе. Дальше обновления идут по USB.</p>
<div class="grid">{fig("proshivka", "Три провода от ST-Link в разъём SWD, питание — от USB-кабеля. Провод 3.3V не нужен.")}
<div class="steps">
<h3>Порядок</h3>
<ol>
<li>Впаяй 4-пиновую гребёнку в первые 4 отверстия <b>SWD (J13)</b>. Если там уже стоит разъём — паять не надо.</li>
<li>Воткни USB-B в компьютер. Найди пин 1: на нём <b>3.3 В</b>. Пин 3 звонится с корпусом USB-разъёма — это земля.</li>
<li>Провода: <b>SWCLK → 2</b>, <b>GND → 3</b>, <b>SWDIO → 4</b>.</li>
<li>STM32CubeProgrammer → ST-LINK, SWD → <b>Connect</b> → файл <code>midi_cv_full.hex</code> → Start Programming.</li>
</ol>
<h3>Разъём J13</h3>
<table class="pins"><tr><th>Пин</th><th>Сигнал</th><th>ST-Link</th></tr>
<tr><td>1</td><td>+3.3 В</td><td>не подключать</td></tr>
<tr><td>2</td><td>SWCLK</td><td><span class="swatch" style="background:var(--w-blue)"></span>SWCLK</td></tr>
<tr><td>3</td><td>GND</td><td><span class="swatch" style="background:var(--fg)"></span>GND</td></tr>
<tr><td>4</td><td>SWDIO</td><td><span class="swatch" style="background:var(--w-amber)"></span>SWDIO</td></tr>
<tr><td>5, 6</td><td>не подключены</td><td>—</td></tr></table>
</div></div></section>''')

body.append(f'''<section class="job" id="usb"><h2>USB: работа и обновление прошивки</h2>
<p class="lede">Один и тот же кабель. Разница только в том, держишь ли ты LEARN в момент подключения.</p>
<div class="pair">{fig("usb_a", "Обычный запуск: плата видна как MIDI-устройство и COM-порт.")}
{fig("usb_b", "С зажатой LEARN: плата ждёт новую прошивку.")}</div>
<div class="steps" style="margin-top:18px">
<h3>Обновить прошивку</h3>
<ol>
<li>Зажми LEARN, воткни USB, отпусти. Или набери <code>dfu</code> в COM-порту.</li>
<li><code>dfu-util -a 0 -D midi_cv_alpha.dfu</code></li>
<li>Через полторы секунды плата сама запустит новую прошивку. Передумал — коротко нажми LEARN.</li>
</ol></div></section>''')

body.append(f'''<section class="job" id="midi"><h2>MIDI</h2>
<p class="lede">Обычный 5-пиновый MIDI-кабель. Вместо клавиатуры можно играть из компьютера по USB — плата принимает оба входа сразу.</p>
<div class="grid">{fig("midi", "MIDI OUT клавиатуры — в нижний DIN. Верхний DIN (THRU) — копия входа для следующего синта.")}
<div class="steps"><h3>Что важно</h3><ul>
<li><b>IN — нижний</b> разъём, <b>THRU — верхний</b>.</li>
<li>THRU работает аппаратно, даже если процессор не прошит.</li>
<li>Всё, что пришло в DIN IN, плата пересылает в компьютер по USB — её можно использовать как MIDI-интерфейс.</li>
<li>Разъёма MIDI OUT на этой плате нет.</li>
</ul></div></div></section>''')

body.append(f'''<section class="job" id="vyhody"><h2>CV, GATE и CLOCK</h2>
<p class="lede">Выходы — площадки на плате. Припаяй к ним провода или штырьки. Землю для синта возьми с пина 3 разъёма SWD.</p>
<div class="grid">{fig("vyhody", "CV — высота ноты 1 В/октаву (или ручка CC), GATE — нота нажата, CLOCK — такт из MIDI Clock.")}
<div class="steps"><h3>Что где</h3>
<table class="pins"><tr><th>Площадка</th><th>Сигнал</th><th>Уровень</th></tr>
<tr><td>J1 «CV»</td><td>высота ноты</td><td>0…10 В</td></tr>
<tr><td>J14 «GATE»</td><td>нота нажата</td><td>0 / 5 В</td></tr>
<tr><td>J31</td><td>CLOCK</td><td>3.3 В, импульс вниз</td></tr>
<tr><td>J2</td><td>срез фильтра</td><td>только измерять</td></tr></table>
<h3>Проверка мультиметром</h3>
<ul><li><code>set jack 5000</code> → на J1 около 5 В</li><li><code>gate on</code> → на J14 около 5 В</li></ul>
<p class="note">J31 — это голая ножка процессора: 3.3 В и инверсная логика (под будущий транзисторный каскад). Подключай только к входам, которые терпят 3.3 В и не подают напряжение обратно.</p>
</div></div></section>''')

body.append(f'''<section class="job" id="filtr"><h2>Фильтр и аудио</h2>
<p class="lede">Верхняя половина — стерео аналоговый фильтр. Срез управляется ручкой FILTER, процессором (CC74 по умолчанию) и внешним CV — всё складывается.</p>
<div class="grid wide">{fig("filtr", "Звук входит в L IN / R IN, выходит из L OUT / R OUT. На белые разъёмы FILTER, RES L, RES R ставятся потенциометры 10 кОм.")}
<div class="steps"><ul>
<li>Распиновку белых разъёмов проверь прозвонкой: земляной контакт звонится с землёй (пин 3 SWD).</li>
<li>Подстроечные резисторы выставлены при сборке — крути их, только если знаешь, что настраиваешь.</li>
</ul></div></div></section>''')

body.append(f'''<section class="job" id="learn"><h2>Кнопка LEARN и светодиод NOTEON</h2>
<p class="lede">Что произойдёт, зависит от того, сколько держать кнопку. Отсчёт идёт с момента нажатия.</p>
<div class="grid wide">{fig("learn", "Шкала удержания кнопки: каждая отметка включает следующий режим.")}
{fig("led", "Как мигает NOTEON: по нему видно, в каком режиме плата.")}</div></section>''')

body.append(f'''<section class="job" id="uart"><h2>Запасная консоль USART1</h2>
<p class="lede">Нужна, только если USB не завёлся. Понадобится отдельный USB-UART переходник на 3.3 В — ST-Link так не умеет.</p>
<div class="grid">{fig("uart", "TX платы идёт на RX переходника и наоборот, земля к земле. Скорость 115200.")}
<div class="steps"><ul>
<li>Разъём J22 — три пина: TX, RX, GND. Порядок проверь прозвонкой: GND звонится с пином 3 SWD.</li>
<li>В терминале: 115200 бод, 8N1. Команды те же, что в COM-порту по USB.</li>
</ul></div></div></section>''')

html = f'''<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Подключение MIDI Analog Filter</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Golos+Text:wght@400;500;600&family=JetBrains+Mono:wght@500;600&family=Unbounded:wght@600&display=swap">
<style>{CSS}</style>
</head><body>
<div class="wrap">
{"".join(body)}
</div>
</body></html>
'''
open(OUT, "w").write(html)
print("written", OUT, len(html))
