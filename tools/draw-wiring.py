"""Draw the project's wiring and rear header map as SVG, PNG and a two-page PDF.

Requires Pillow. Pin positions are transcribed from the cited rear photographs;
A/B, row numbers and L/R are drawing references, not manufacturer pin numbers.
"""
from pathlib import Path
from html import escape
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "schema"
OUT.mkdir(parents=True, exist_ok=True)
W, H, SCALE = 1800, 1273, 2
INK, MUTED, BORDER = "#172b3a", "#536777", "#c7d3dc"
RED, GREEN, GROUND, FLOW, BUTTON, RELAY = "#bb303a", "#087746", "#273b4a", "#b86609", "#175fa2", "#77512c"


class Page:
    def __init__(self, title, subtitle, number):
        self.image = Image.new("RGB", (W*SCALE, H*SCALE), "white")
        self.draw = ImageDraw.Draw(self.image)
        self.svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
                    '<rect width="100%" height="100%" fill="white"/>']
        self.text(60, 38, title, 37, bold=True)
        self.text(60, 96, subtitle, 23, MUTED)
        self.line([(60, 145), (1740, 145)], BORDER, 2)
        self.text(60, 1227, "mux_temperatura · firmware 0.3.0 · 6 novembre 2026", 17, MUTED)
        self.text(1740, 1227, f"{number} / 2", 17, MUTED, anchor="end")

    def text(self, x, y, value, size=23, color=INK, bold=False, anchor="start"):
        font = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf", size*SCALE)
        self.draw.text((x*SCALE, y*SCALE), value, font=font, fill=color,
                       anchor={"start":"lt", "middle":"mt", "end":"rt"}[anchor])
        self.svg.append(f'<text x="{x}" y="{y}" dominant-baseline="text-before-edge" text-anchor="{anchor}" '
                        f'font-family="Arial,sans-serif" font-size="{size}" font-weight="{700 if bold else 400}" '
                        f'fill="{color}">{escape(value)}</text>')

    def box(self, x, y, w, h, fill="white", stroke=BORDER, width=2):
        self.draw.rectangle((x*SCALE, y*SCALE, (x+w)*SCALE, (y+h)*SCALE), fill=fill, outline=stroke, width=width*SCALE)
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>')

    def line(self, points, color=INK, width=3):
        self.draw.line([(x*SCALE, y*SCALE) for x,y in points], fill=color, width=width*SCALE, joint="curve")
        pts = " ".join(f"{x},{y}" for x,y in points)
        self.svg.append(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round"/>')

    def dot(self, x, y, color=INK, radius=6, fill=None):
        fill = color if fill is None else fill
        self.draw.ellipse(((x-radius)*SCALE,(y-radius)*SCALE,(x+radius)*SCALE,(y+radius)*SCALE), fill=fill, outline=color, width=2*SCALE)
        self.svg.append(f'<circle cx="{x}" cy="{y}" r="{radius}" fill="{fill}" stroke="{color}" stroke-width="2"/>')

    def save(self, stem):
        self.image.save(OUT / (stem+".png"), dpi=(308,308))
        (OUT / (stem+".svg")).write_text("\n".join(self.svg)+"\n</svg>\n", encoding="utf-8")


p = Page("ESP32 Relay X4 · schema dei collegamenti", "Bus sonde unico · flussostato a contatto pulito · separazione verso la PDC tramite relè", 1)
p.box(60, 190, 500, 970, "#f3f7fa")
p.text(90, 215, "SCHEDA ESP32 RELAY X4", 27, bold=True)
p.text(90, 260, "Posizioni fisiche dei pin: pagina 2", 21, MUTED)

for label, ref, y, color in [("3V3", "A10-L oppure B1-R", 370, RED),
                             ("GPIO4 · DQ sonde", "A7-R", 450, GREEN),
                             ("GND", "A1-R / A4-R / B10-R", 530, GROUND),
                             ("GPIO22 · ingresso", "A2-R · da verificare sul proprio esemplare", 700, FLOW),
                             ("GPIO0 · pulsante", "A7-L · pulsante BOOT già presente", 900, BUTTON)]:
    p.text(90, y-44, label, 25, color, bold=True)
    p.text(90, y-12, ref, 17, MUTED)
    p.dot(560,y,color,7)

p.text(90, 570, "LED di stato integrato → GPIO23", 22, "#7952a0", True)
p.text(90, 600, "Secondo LED extra: alimentazione presunta;", 18, MUTED)
p.text(90, 624, "associazione D13/D14 ancora da verificare.", 18, MUTED)

p.box(1120, 230, 620, 355, "#f5fbf7")
p.text(1150, 252, "SONDE DS18B20 · S1 … S8", 26, GREEN, True)
p.text(1150, 294, "Ogni sonda sugli stessi tre segnali", 22)
for y, color, label in [(370,RED,"VDD  +3,3 V"),(450,GREEN,"DQ  comune a tutte le sonde"),(530,GROUND,"GND  comune")]:
    p.line([(560,y),(1160,y)],color,4)
    p.dot(1160,y,color,7)
    p.text(1190,y-15,label,24,color,True)
p.line([(800,370),(800,391)],RED,3)
p.box(790,391,20,38,"white",INK,3)
p.line([(800,429),(800,450)],GREEN,3)
p.dot(800,370,RED)
p.dot(800,450,GREEN)
p.text(830,391,"4,7 kΩ",22,INK,True)
p.text(830,422,"una sola per bus",17,MUTED)
p.text(1150,552,"Pin delle sonde da identificare: non affidarsi ai colori.",17,MUTED)

# Common ESP32-side ground; never extended to the PDC input.
p.line([(940,530),(940,960)],GROUND,3)
p.dot(940,530,GROUND)

def contact_panel(y, title, subtitle, color, return_y):
    p.box(1120,y-80,620,170,"white")
    p.text(1150,y-61,title,25,color,True)
    p.line([(560,y),(1260,y)],color,4)
    p.dot(1260,y,color,6,"white")
    p.line([(1267,y),(1482,y-28)],color,4)
    p.dot(1510,y,color,6,"white")
    p.line([(1517,y),(1700,y),(1700,return_y),(940,return_y)],GROUND,3)
    p.dot(940,return_y,GROUND)
    p.text(1150,y+30,subtitle,18,MUTED)

contact_panel(700,"FLUSSOSTATO · contatto pulito", "Chiuso: GPIO22 a GND. Pull-up interna abilitata.",FLOW,770)
contact_panel(900,"PULSANTE AP / RESET", "5–15 s + rilascio: AP. Da 15 s + rilascio: reset.",BUTTON,960)
p.line([(921,975),(959,975)],GROUND,3)
p.line([(928,984),(952,984)],GROUND,3)
p.line([(935,993),(945,993)],GROUND,3)

p.box(90,1008,440,132,"#fffaf2",RELAY)
p.text(110,1022,"RELÈ FISICO 4 · GPIO26",24,RELAY,True)
p.text(110,1066,"COM4",22,RELAY,True)
p.text(110,1110,"NO4  (NC4 non usato)",21,RELAY,True)
p.dot(560,1070,RELAY,7)
p.dot(560,1130,RELAY,7)
p.box(1120,1010,620,150,"#fffaf2",RELAY)
p.text(1150,1024,"INGRESSO FLUSSOSTATO DELLA PDC",23,RELAY,True)
p.line([(350,1070),(560,1070),(1160,1070)],RELAY,4)
p.line([(420,1130),(560,1130),(1160,1130)],RELAY,4)
for y,label in [(1070,"Morsetto ingresso 1"),(1130,"Morsetto ingresso 2")]:
    p.dot(1160,y,RELAY,6)
    p.text(1190,y-14,label,22)
p.text(620,1176,"Solo contatti COM/NO verso PDC: nessun collegamento di massa tramite questi due fili.",20,RELAY)
p.text(60,1201,"Punto pieno = giunzione. Incrocio senza punto = fili non collegati.",16,MUTED)
p.save("01-collegamenti-relay-x4")

q = Page("ESP32 Relay X4 · mappa fisica dei pin", "VISTA RETRO: lato saldature, sezione alimentazione a destra, morsetti dei relè in basso", 2)
q.text(60,171,"A / B, righe 1–10 e L / R sono riferimenti di questo disegno; non sono numeri stampati sulla scheda.",21,MUTED)

A=[("G23","GND"),("TX0","G22"),("G21","RX0"),("G19","GND"),("G5","G18"),
   ("G16","G17"),("G0","G4"),("G15","G2"),("SD0","SD1"),("3V3","CLK")]
B=[("GND","3V3"),("EN","SVP"),("SVN","G34"),("G35","G32"),("G33","G25"),
   ("G26","G27"),("G14","G12"),("G13","SD2"),("SD3","CMD"),("5V","GND")]
colors={"G23":"#7952a0","G22":FLOW,"G0":BUTTON,"G4":GREEN,"3V3":RED,"GND":GROUND,
        "G26":RELAY,"G32":RELAY,"G33":RELAY,"G25":RELAY}

def header(x, letter, rows, caption):
    q.box(x,230,790,550,"#f5f8fa")
    q.text(x+25,248,f"{letter} · {caption}",27,bold=True)
    q.text(x+327,291,"L",21,MUTED,anchor="middle")
    q.text(x+407,291,"R",21,MUTED,anchor="middle")
    for i,(left,right) in enumerate(rows):
        y=340+i*43
        q.text(x+28,y-14,f"r{i+1}",20,MUTED)
        for label, cx, tx, anchor in [(left,x+327,x+270,"end"),(right,x+407,x+465,"start")]:
            color=colors.get(label,MUTED)
            q.dot(cx,y,color,10,"white")
            q.text(tx,y-16,label,25,color,label in colors,anchor)
    q.text(x+25,795,"Gxx = GPIOxx; leggere le colonne dal lato posteriore.",18,MUTED)

header(60,"A",A,"connettore a sinistra nella foto")
header(950,"B",B,"connettore centrale nella foto")
q.text(60,840,"COLLEGAMENTI PRINCIPALI",24,bold=True)
q.text(60,881,"Sonde: GPIO4 = A7-R   ·   +3V3 = A10-L / B1-R   ·   GND = A1-R / A4-R / B10-R",23)
q.text(60,920,"Flussostato: GPIO22 = A2-R verso GND   ·   Pulsante: GPIO0 = A7-L verso GND",23)
q.text(60,959,"GPIO23 = A1-L: LED di stato documentato. D13/D14: assegnazione da verificare col tester.",22)

q.text(60,1012,"MORSETTI RELÈ · VISTA FRONTALE, lato componenti, come nella foto Amazon",24,RELAY,True)
for i,gpio in enumerate([32,33,25,26]):
    x=60+i*425
    color=RELAY if i==3 else MUTED
    q.box(x,1054,400,134,"#fffaf2" if i==3 else "#f5f8fa",color)
    q.text(x+200,1067,f"Relè {i+1} · GPIO{gpio}",23,color,True,"middle")
    for j,label in enumerate([f"NO{i+1}",f"COM{i+1}",f"NC{i+1}"]):
        cx=x+65+j*130
        q.dot(cx,1125,color,9,"white")
        q.text(cx,1150,label,21,color,i==3 and j<2,"middle")
q.text(60,1197,"Girando la scheda l'ordine si specchia: usare sempre le sigle COM4 / NO4 serigrafate sul fronte.",19,RELAY)
q.save("02-pinout-retro-relay-x4")

p.image.save(OUT/"schema-relay-x4.pdf","PDF",resolution=308,save_all=True,append_images=[q.image],
             title="ESP32 Relay X4 - Collegamenti e pinout",author="mux_temperatura")
print(OUT/"schema-relay-x4.pdf")
