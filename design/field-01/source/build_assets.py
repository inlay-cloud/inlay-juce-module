"""Build FIELD / 01 vector artwork. Requires fonttools; see ../README.md."""
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
import math, json, re, base64

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets'
ASSETS.mkdir(exist_ok=True)
INK = '#343a31'
MUTED = '#777b69'
IVORY = '#e5e1cc'
fonts = {
    'regular': TTFont('/System/Library/Fonts/Supplemental/Arial.ttf'),
    'bold': TTFont('/System/Library/Fonts/Supplemental/Arial Bold.ttf'),
    'mono': TTFont('/System/Library/Fonts/Supplemental/Andale Mono.ttf'),
}

def text(s, x, y, size=12, fill=INK, font='regular', spacing=0, anchor='start'):
    f = fonts[font]; gs = f.getGlyphSet(); cmap = f.getBestCmap()
    scale = size / f['head'].unitsPerEm
    width = sum(f['hmtx'][cmap[ord(c)]][0] * scale for c in s) + spacing * max(0, len(s)-1)
    if anchor == 'middle': x -= width / 2
    if anchor == 'end': x -= width
    out = []
    for c in s:
        g = cmap[ord(c)]; p = SVGPathPen(gs); gs[g].draw(p)
        if p.getCommands(): out.append(f'<path d="{p.getCommands()}" transform="translate({x:.3f} {y}) scale({scale:.6f} {-scale:.6f})" fill="{fill}"/>')
        x += f['hmtx'][g][0] * scale + spacing
    return ''.join(out)

def rect(x,y,w,h,fill,rx=0,extra=''):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" {extra}/>'

def line(x1,y1,x2,y2,color=INK,width=1,extra=''):
    return f'<path d="M{x1} {y1} L{x2} {y2}" fill="none" stroke="{color}" stroke-width="{width}" {extra}/>'

def circle(x,y,r,fill,extra=''):
    return f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}" {extra}/>'

def gradient(id, stops, radial=False):
    tag = 'radialGradient' if radial else 'linearGradient'
    props = 'cx="35%" cy="20%" r="90%"' if radial else 'x1="0" y1="0" x2="0" y2="1"'
    return f'<{tag} id="{id}" {props}>' + ''.join(f'<stop offset="{n/(len(stops)-1):.3f}" stop-color="{c}"/>' for n,c in enumerate(stops)) + f'</{tag}>'

def svg(w,h,body):
    return f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{w}" height="{h}" viewBox="0 0 {w} {h}">{body}</svg>'

art = {}
def save(name,w,h,body):
    art[name]=(w,h,body)
    (ASSETS/f'{name}.svg').write_text(svg(w,h,body))

def placed(name,x,y,w=None,h=None,suffix=''):
    aw,ah,b = art[name]
    ids = re.findall(r'id="([^"]+)"', b)
    for old in ids:
        new = name.replace('-','_') + suffix + '_' + old
        b = b.replace(f'id="{old}"', f'id="{new}"').replace(f'url(#{old})', f'url(#{new})')
    return f'<g transform="translate({x} {y}) scale({(w or aw)/aw} {(h or ah)/ah})">{b}</g>'

# Case and printed faceplate are independent full-size layers.
b = '<defs>'+gradient('enamel',['#eeebdb','#ddd9c3'])+gradient('edge',['#65695b','#33392f'])+'</defs>'
b += rect(0,0,1080,700,'#272d27',20)+rect(2,2,1076,696,'url(#edge)',18)
b += rect(8,8,1064,680,'url(#enamel)',13, 'stroke="#f6f2e2" stroke-width="1"')
b += line(24,687,1056,687,'#151d17',2)
save('panel',1080,700,b)

b = text('FIELD',52,65,34,font='bold',spacing=6)
b += text('/ 01',214,64,27,fill=MUTED,spacing=1)
b += text('ANALOG OSCILLATOR',54,88,10,spacing=2.4)
b += text('INLAY',1026,57,14,font='bold',spacing=3,anchor='end')
b += text('INSTRUMENT SERIES',1026,79,9,spacing=1.7,anchor='end')
b += line(54,111,1026,111,'#a6aa97')
b += text('01',752,165,10,font='mono',fill=MUTED)
b += text('WAVEFORM',786,165,12,font='bold',spacing=1.5)
b += text('02',752,286,10,font='mono',fill=MUTED)
b += text('RANGE',786,286,12,font='bold',spacing=1.5)
b += text('LOW',780,347,10,spacing=1.5)
b += text('AUDIO',996,347,10,spacing=1.5,anchor='end')
b += line(752,258,1026,258,'#b7baa6')
b += line(54,415,1026,415,'#a6aa97')
b += text('GENERATOR',54,443,9,spacing=2,fill=MUTED)
b += text('CONTINUOUS / FREE RUNNING',1026,435,8,spacing=1.1,fill=MUTED,anchor='end')
knobs = [('frequency','FREQUENCY','03',167,0.46,'220.0 Hz'),('drift','DRIFT','04',416,0.27,'12 %'),('colour','COLOUR','05',664,0.60,'58 %'),('level','LEVEL','06',913,0.70,'−12 dB')]
for i,(pid,label,no,cx,val,readout) in enumerate(knobs):
    b += text(label,cx,631,11,font='bold',spacing=1.25,anchor='middle')
    if i<3: b += line(cx+124,488,cx+124,605,'#c3c5b1')
save('panel-lettering',1080,700,b)

# Faceplate fixation: small slotted fastener with no decorative motifs.
b = '<defs>'+gradient('screw',['#edead8','#909785'])+'</defs>'
b += circle(10,11,7.5,'#747b69')+circle(10,10,6.4,'url(#screw)','stroke="#939985" stroke-width="0.7"')
b += line(6.5,11.5,13.5,8.5,'#68705f',1.4)
save('fastener',20,20,b)

# Scope: glass, graticule, data overlay and trace are separate assets.
b = '<defs>'+gradient('rim',['#737a65','#f4f0dc'])+gradient('bezel',['#333d32','#18271f'])+'</defs>'
b += rect(0,0,650,246,'url(#rim)',15)
b += rect(2,2,646,241,'url(#bezel)',13)
b += rect(11,11,628,223,'#0e1913',8,'stroke="#0c1510" stroke-width="2"')
save('scope-bezel',650,246,b)
b = '<defs>'+gradient('glass',['#293b2c','#15251d'],True)+'</defs>'
b += rect(0,0,622,217,'url(#glass)',6)
for x in range(31,622,56): b += line(x,35,x,182,'#466147',0.65,'opacity="0.38"')
for y in range(49,183,33): b += line(17,y,605,y,'#466147',0.65,'opacity="0.38"')
b += line(17,115,605,115,'#779070',0.7,'opacity="0.45"')
b += line(311,36,311,182,'#779070',0.7,'opacity="0.45"')
for x in range(17,606,14): b += line(x,112,x,118,'#67825f',0.6)
save('scope-glass-grid',622,217,b)
b = text('OSC / A',20,24,10,fill='#a8b995',font='mono',spacing=1)
b += text('SINE',602,24,10,fill='#a8b995',font='mono',spacing=1,anchor='end')
b += circle(23,199,2.5,'#c2d9a1')+text('SIGNAL',34,203,9,fill='#91a985',font='mono',spacing=0.9)
b += text('220.0 Hz',602,203,10,fill='#c5d9ad',font='mono',anchor='end')
save('scope-readout-example',622,217,b)
b=''
for wave in ['sine','triangle','pulse']:
    points=[]
    for i in range(581):
        t=i/580*2.35
        v=math.sin(t*2*math.pi) if wave=='sine' else (2/math.pi*math.asin(math.sin(t*2*math.pi)) if wave=='triangle' else (1 if math.sin(t*2*math.pi)>=0 else -1))
        points.append(f'{i+20:.2f},{115-51*v:.2f}')
    d='M'+' L'.join(points)
    b=f'<path d="{d}" fill="none" stroke="#accb8d" stroke-width="7" opacity="0.045"/>'
    b+=f'<path d="{d}" fill="none" stroke="#accb8d" stroke-width="3.4" opacity="0.17"/>'
    b+=f'<path d="{d}" fill="none" stroke="#c6dca6" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round"/>'
    save('scope-trace-'+wave,622,217,b)

# Dial bodies remain fixed in place; only the index rotates.
for light in [False,True]:
    name='knob-sage' if light else 'knob-charcoal'
    colors=['#aeb69b','#758469','#586951'] if light else ['#515748','#333b30','#252e25']
    b='<defs>'+gradient('body',colors)+gradient('cap',colors,True)+gradient('foot',['#b5b7a1','#6a705d'])+'</defs>'
    b+=f'<ellipse cx="80" cy="86" rx="63" ry="61" fill="#5a624f" opacity="0.12"/>'
    b+=f'<ellipse cx="80" cy="83" rx="59" ry="59" fill="#626954" opacity="0.15"/>'
    b+=circle(80,80,58,'url(#foot)')+circle(80,79,55,'#222c22')
    b+=circle(80,78,53,'url(#body)','stroke="#606a53" stroke-width="0.65"')
    for a in range(0,360,6):
        r=math.radians(a)
        b+=line(80+49*math.sin(r),78-49*math.cos(r),80+52*math.sin(r),78-52*math.cos(r),'#172319' if not light else '#42543c',0.7,'opacity="0.48"')
    b+=circle(80,78,46,'url(#cap)','stroke="#19281c" stroke-width="0.8"')
    b+=circle(80,77,44.6,'none','stroke="#dce2c4" stroke-width="0.55" opacity="0.24"')
    save(name,160,160,b)
b = line(80,41,80,56,'#e7ead1',3,'stroke-linecap="round"')
save('knob-index',160,160,b)
b=''
for i in range(25):
    a=math.radians(-135+i*270/24)
    r1=69 if i%4==0 else 72
    b+=line(80+r1*math.sin(a),80-r1*math.cos(a),80+76*math.sin(a),80-76*math.cos(a),'#838a73',1.25 if i%4==0 else 0.8)
save('knob-scale',160,160,b)

# Three-position wave selector, with all complete parameter states.
for selected in range(3):
    b='<defs>'+gradient('switch',['#f0edda','#d6d6bf'])+'</defs>'
    b+=rect(0,0,274,57,'#b0b59f',7)+rect(1,1,272,54,'#c4c8b2',6)
    for i in range(3):
        xx=3+i*90
        b+=rect(xx,3,88,49,'#45553e' if i==selected else 'url(#switch)',4)
        if i==selected: b+=line(xx+15,48,xx+73,48,'#cbd7b7',1)
        if i==0: d=f'M{xx+25} 27 C{xx+32} 8 {xx+39} 8 {xx+44} 27 S{xx+58} 46 {xx+64} 27'
        elif i==1: d=f'M{xx+23} 33 L{xx+34} 17 L{xx+54} 37 L{xx+66} 21'
        else: d=f'M{xx+23} 35 L{xx+23} 18 L{xx+44} 18 L{xx+44} 35 L{xx+65} 35 L{xx+65} 18'
        b+=f'<path d="{d}" fill="none" stroke="{IVORY if i==selected else INK}" stroke-width="1.6" stroke-linejoin="round"/>'
    save('wave-selector-'+['sine','triangle','pulse'][selected],274,57,b)

for high in [False, True]:
    b='<defs>'+gradient('lever',['#f6f1dc','#b2b9a2','#8f9c83'])+'</defs>'
    b+=rect(0,0,90,40,'#adb59d',20)+rect(2,2,86,36,'#4f5c46',18)
    b+=rect(5,5,80,29,'#2d3d2e',14)
    xx=52 if high else 4
    b+=rect(xx,4,34,33,'#1a2b1d',15)
    b+=rect(xx+1,3,32,31,'url(#lever)',14,'stroke="#e3e7cf" stroke-width="0.7"')
    b+=line(xx+17,10,xx+17,26,'#74806a',1.2)
    save('range-'+('audio' if high else 'low'),90,40,b)

preview = placed('panel',0,0)
texture=ASSETS/'enamel-texture.png'
if texture.exists():
    encoded=base64.b64encode(texture.read_bytes()).decode()
    preview+=f'<image x="10" y="10" width="1060" height="674" opacity="0.16" preserveAspectRatio="none" xlink:href="data:image/png;base64,{encoded}"/>'
preview+=placed('panel-lettering',0,0)
for i,(x,y) in enumerate([(21,22),(1039,22),(21,650),(1039,650)]): preview+=placed('fastener',x,y,suffix=str(i))
preview+=placed('scope-bezel',54,140)+placed('scope-glass-grid',68,154)+placed('scope-trace-sine',68,154)+placed('scope-readout-example',68,154)
preview+=placed('wave-selector-sine',752,185)+placed('range-audio',844,320)
for i,(pid,label,no,cx,val,readout) in enumerate(knobs):
    preview+=placed('knob-scale',cx-80,462,suffix=str(i))
    preview+=placed('knob-sage' if i==0 else 'knob-charcoal',cx-80,462,suffix=str(i))
    preview+=f'<g transform="translate({cx-80} 462)"><g transform="rotate({-135+270*val:.2f} 80 80)">{art["knob-index"][2]}</g></g>'
    preview+=text(readout,cx,656,12,fill=MUTED,font='mono',anchor='middle')
(ROOT/'field-01-preview.svg').write_text(svg(1080,700,preview))

# JUCE's SVG Drawable derives bounds from visible shapes. The complete base has the
# panel canvas as its first layer, so it keeps the authored 1080 x 700 geometry.
# Runtime code overlays live waveform, selector, range, values and dial pointers.
runtime_base = placed('panel',0,0)
if texture.exists():
    runtime_base+=f'<image x="10" y="10" width="1060" height="674" opacity="0.16" preserveAspectRatio="none" xlink:href="data:image/png;base64,{encoded}"/>'
runtime_base+=placed('panel-lettering',0,0)
for i,(x,y) in enumerate([(21,22),(1039,22),(21,650),(1039,650)]): runtime_base+=placed('fastener',x,y,suffix='runtime'+str(i))
runtime_base+=placed('scope-bezel',54,140)+placed('scope-glass-grid',68,154)
for i,(pid,label,no,cx,val,readout) in enumerate(knobs):
    runtime_base+=placed('knob-scale',cx-80,462,suffix='runtime'+str(i))
    runtime_base+=placed('knob-sage' if i==0 else 'knob-charcoal',cx-80,462,suffix='runtime'+str(i))
(ROOT/'field-01-runtime-base.svg').write_text(svg(1080,700,runtime_base))

manifest={
    'name':'FIELD / 01', 'canvas':{'width':1080,'height':700},
    'palette':{'ivory':IVORY,'ink':INK,'muted':MUTED,'phosphor':'#c6dca6'},
    'layers':[
        {'asset':'panel.svg','bounds':[0,0,1080,700]},
        {'asset':'enamel-texture.png','bounds':[10,10,1060,674],'opacity':0.16,'optional':True},
        {'asset':'panel-lettering.svg','bounds':[0,0,1080,700]},
        {'asset':'scope-bezel.svg','bounds':[54,140,650,246]},
        {'asset':'scope-glass-grid.svg','bounds':[68,154,622,217]},
    ],
    'scope':{'bounds':[68,154,622,217],'traceArea':[88,190,582,147],'readoutIsExample':True},
    'controls':[
        {'id':'waveform','kind':'choice','bounds':[752,185,274,57],'states':['sine','triangle','pulse'],'default':'sine','assetPattern':'wave-selector-{state}.svg'},
        {'id':'range','kind':'toggle','bounds':[844,320,90,40],'hitBounds':[780,309,216,62],'states':['low','audio'],'default':'audio','assetPattern':'range-{state}.svg'},
    ]+[
        {'id':pid,'kind':'rotary','bounds':[cx-80,462,160,160],'hitBounds':[cx-67,477,134,134], 'pivot':[80,80], 'rotationDegrees':[-135,135], 'defaultNormalized':val, 'previewValue':readout,'bodyAsset':'knob-sage.svg' if i==0 else 'knob-charcoal.svg','scaleAsset':'knob-scale.svg','rotatingAsset':'knob-index.svg','valueBaseline':[cx,656]}
        for i,(pid,label,no,cx,val,readout) in enumerate(knobs)
    ],
    'fasteners':[[21,22,20,20],[1039,22,20,20],[21,650,20,20],[1039,650,20,20]],
    'notes':['Suggested parameter semantics only; no DSP implemented.', 'Preview values are illustrative, not a calibrated parameter mapping.', 'Rotate index only; leave body lighting fixed.', 'All SVG typography is outlined. Dynamic text and waveform should be drawn in code.'],
}
(ROOT/'layout.json').write_text(json.dumps(manifest,indent=2)+'\n')
sheet = rect(0,0,1080,1050,'#eeebdd')
sheet += text('FIELD / 01',44,55,27,font='bold',spacing=3)
sheet += text('COMPONENT LIBRARY',1036,54,11,spacing=2,anchor='end')
sheet += line(44,79,1036,79,'#adb29d')
sheet += text('01 / ROTARY ASSEMBLY',44,118,11,spacing=1.4)
for i,name in enumerate(['knob-sage','knob-charcoal','knob-scale','knob-index']):
    x=48+i*249
    if name == 'knob-index': sheet+=rect(x+25,142,160,160,'#343e31',8)
    sheet+=placed(name,x+25,142,suffix='sheet'+str(i))
    sheet+=text(name,x+105,325,11,font='mono',anchor='middle')
sheet+=text('02 / WAVEFORM STATES',44,382,11,spacing=1.4)
for i,state in enumerate(['sine','triangle','pulse']):
    x=44+i*344
    sheet+=placed('wave-selector-'+state,x,410,suffix='sheet')
    sheet+=text(state.upper(),x,492,10,spacing=1.8)
sheet+=text('03 / RANGE STATES',44,553,11,spacing=1.4)
sheet+=placed('range-low',48,587,suffix='sheet')+placed('range-audio',212,587,suffix='sheet')
sheet+=text('LOW',93,652,10,anchor='middle',spacing=1.4)+text('AUDIO',257,652,10,anchor='middle',spacing=1.4)
sheet+=text('04 / SCOPE LAYERS',404,553,11,spacing=1.4)
sheet+=placed('scope-bezel',404,576,632,239,suffix='sheet')
sheet+=placed('scope-glass-grid',418,590,604,211,suffix='sheet')
sheet+=placed('scope-trace-sine',418,590,604,211,suffix='sheet')
sheet+=placed('scope-readout-example',418,590,604,211,suffix='sheet')
sheet+=text('05 / MATERIAL + HARDWARE',44,724,11,spacing=1.4)
if texture.exists(): sheet+=f'<image x="44" y="749" width="224" height="121" preserveAspectRatio="xMidYMid slice" xlink:href="data:image/png;base64,{encoded}"/>'
sheet+=placed('fastener',302,790,34,34,suffix='sheet')
sheet+=line(44,919,1036,919,'#adb29d')
sheet+=text('18 VECTOR ASSETS / 1 RASTER MATERIAL',44,958,11,spacing=1.5)
sheet+=text('Native canvas 1080 x 700. Independent layers; alternate switch and waveform states included.',44,984,12,fill=MUTED)
sheet+=text('See layout.json for bounds, pivots, hit areas and parameter identifiers.',44,1006,12,fill=MUTED)
(ROOT/'asset-sheet.svg').write_text(svg(1080,1050,sheet))
print(f'Built {len(art)} SVG assets and composed preview in {ROOT}')
