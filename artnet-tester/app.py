#!/usr/bin/env python3
import json
import os
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ARTNET_PORT = 6454
DMX_SLOTS = 512

target_ip = os.environ.get("ARTNET_TARGET", "192.168.44.1")
universe = int(os.environ.get("ARTNET_UNIVERSE", "0"))
http_port = int(os.environ.get("HTTP_PORT", "8080"))
default_dpx_address = int(os.environ.get("DPX_ADDRESS", "450"))
default_pixezoom_addresses = os.environ.get("PIXEZOOM_ADDRESSES", "100,107,114,121")
default_pixezoom_channels = int(os.environ.get("PIXEZOOM_CHANNELS", "7"))

dmx = bytearray(DMX_SLOTS)
sequence = 1
frames_sent = 0
last_action = "start"
lock = threading.Lock()
program_thread = None
program_stop = threading.Event()


def clamp(value, default=0):
    try:
        return max(0, min(255, int(value)))
    except (TypeError, ValueError):
        return default


def parse_addresses(value):
    addresses = []
    for part in str(value).replace(";", ",").split(","):
        part = part.strip()
        if not part:
            continue
        try:
            address = int(part)
        except ValueError:
            continue
        if 1 <= address <= DMX_SLOTS:
            addresses.append(address)
    return addresses or [100, 107, 114, 121]


def artdmx_packet(payload):
    global sequence
    length = len(payload)
    packet = bytearray()
    packet.extend(b"Art-Net\0")
    packet.extend((0x00, 0x50))
    packet.extend((0x00, 0x0e))
    packet.append(sequence)
    packet.append(0x00)
    packet.append(universe & 0xff)
    packet.append((universe >> 8) & 0xff)
    packet.append((length >> 8) & 0xff)
    packet.append(length & 0xff)
    packet.extend(payload)
    sequence = 1 if sequence >= 255 else sequence + 1
    return bytes(packet)


def send_frame():
    global frames_sent
    with lock:
        payload = bytes(dmx)
        packet = artdmx_packet(payload)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.sendto(packet, (target_ip, ARTNET_PORT))
    frames_sent += 1


def set_range(address, values):
    if address < 1 or address > DMX_SLOTS:
        return
    with lock:
        for i, value in enumerate(values):
            slot = address - 1 + i
            if 0 <= slot < DMX_SLOTS:
                dmx[slot] = clamp(value)
    send_frame()


def set_she_fs403a(address, dim, red, green, blue, white, mode, strobe):
    set_range(address, [dim, red, green, blue, white, mode, strobe])


def set_dpx620(address, values, channels=6, master=255):
    channels = max(1, min(6, int(channels or 6)))
    master = clamp(master, 255)
    scaled = []
    for index in range(channels):
        value = values[index] if index < len(values) else 0
        scaled.append((clamp(value) * master) // 255)
    set_range(address, scaled)


def set_pixezoom(addresses, values, channels=7, master=255):
    channels = max(1, min(12, int(channels or default_pixezoom_channels)))
    master = clamp(master, 255)
    frame_values = []
    for index in range(channels):
        value = values[index] if index < len(values) else 0
        frame_values.append((clamp(value) * master) // 255)
    with lock:
        for address in addresses:
            if address < 1 or address > DMX_SLOTS:
                continue
            for i, value in enumerate(frame_values):
                slot = address - 1 + i
                if 0 <= slot < DMX_SLOTS:
                    dmx[slot] = value
    send_frame()


def blackout():
    with lock:
        dmx[:] = b"\x00" * DMX_SLOTS
    send_frame()


def stop_program():
    global program_thread
    program_stop.set()
    if program_thread and program_thread.is_alive():
        program_thread.join(timeout=1.0)
    program_thread = None
    program_stop.clear()


def start_program(name, address):
    global program_thread, last_action
    stop_program()
    last_action = f"program:{name}"
    program_thread = threading.Thread(target=run_program, args=(name, address), daemon=True)
    program_thread.start()


def start_dpx_program(name, address, channels, master):
    global program_thread, last_action
    stop_program()
    last_action = f"dpx:{name}"
    program_thread = threading.Thread(target=run_dpx_program, args=(name, address, channels, master), daemon=True)
    program_thread.start()


def start_pixezoom_program(name, addresses, channels, master):
    global program_thread, last_action
    stop_program()
    last_action = f"pixezoom:{name}"
    program_thread = threading.Thread(target=run_pixezoom_program, args=(name, addresses, channels, master), daemon=True)
    program_thread.start()


def run_program(name, address):
    programs = {
        "blueShades": [
            (220, [70, 0, 10, 80, 0, 0, 0]),
            (220, [110, 0, 25, 130, 0, 0, 0]),
            (220, [155, 0, 55, 190, 0, 0, 0]),
            (220, [210, 0, 95, 255, 0, 0, 0]),
            (220, [255, 20, 150, 255, 20, 0, 0]),
            (220, [230, 0, 220, 255, 35, 0, 0]),
            (220, [190, 0, 140, 255, 10, 0, 0]),
            (220, [140, 0, 70, 210, 0, 0, 0]),
        ],
        "orangeShades": [
            (220, [70, 180, 35, 0, 0, 0, 0]),
            (220, [110, 220, 55, 0, 0, 0, 0]),
            (220, [155, 255, 80, 0, 0, 0, 0]),
            (220, [210, 255, 115, 0, 10, 0, 0]),
            (220, [255, 255, 150, 20, 25, 0, 0]),
            (220, [230, 255, 95, 0, 5, 0, 0]),
            (220, [185, 235, 65, 0, 0, 0, 0]),
            (220, [130, 210, 45, 0, 0, 0, 0]),
        ],
        "rgb": [
            (500, [255, 255, 0, 0, 0, 0, 0]),
            (500, [255, 0, 255, 0, 0, 0, 0]),
            (500, [255, 0, 0, 255, 0, 0, 0]),
            (500, [255, 0, 0, 0, 255, 0, 0]),
        ],
        "strobe": [
            (350, [255, 255, 255, 255, 255, 0, 70]),
            (350, [255, 255, 255, 255, 255, 0, 150]),
            (350, [255, 255, 255, 255, 255, 0, 230]),
            (600, [255, 0, 0, 0, 0, 0, 0]),
        ],
    }
    steps = programs.get(name, programs["rgb"])
    while not program_stop.is_set():
        for ms, values in steps:
            if program_stop.is_set():
                break
            set_range(address, values)
            program_stop.wait(ms / 1000)


def run_dpx_program(name, address, channels, master):
    ch = max(1, min(6, int(channels or 6)))
    scenes = {
        "all": [(800, [255, 255, 255, 255, 255, 255])],
        "oneByOne": [
            (350, [255, 0, 0, 0, 0, 0]),
            (350, [0, 255, 0, 0, 0, 0]),
            (350, [0, 0, 255, 0, 0, 0]),
            (350, [0, 0, 0, 255, 0, 0]),
            (350, [0, 0, 0, 0, 255, 0]),
            (350, [0, 0, 0, 0, 0, 255]),
        ],
        "softWave": [
            (450, [255, 170, 80, 20, 0, 0]),
            (450, [170, 255, 170, 80, 20, 0]),
            (450, [80, 170, 255, 170, 80, 20]),
            (450, [20, 80, 170, 255, 170, 80]),
            (450, [0, 20, 80, 170, 255, 170]),
            (450, [0, 0, 20, 80, 170, 255]),
        ],
        "alternate": [
            (500, [255, 0, 255, 0, 255, 0]),
            (500, [0, 255, 0, 255, 0, 255]),
        ],
        "pulse": [
            (180, [30, 30, 30, 30, 30, 30]),
            (180, [90, 90, 90, 90, 90, 90]),
            (180, [170, 170, 170, 170, 170, 170]),
            (260, [255, 255, 255, 255, 255, 255]),
            (180, [120, 120, 120, 120, 120, 120]),
        ],
        "chasePairs": [
            (420, [255, 255, 0, 0, 0, 0]),
            (420, [0, 0, 255, 255, 0, 0]),
            (420, [0, 0, 0, 0, 255, 255]),
            (420, [0, 0, 255, 255, 0, 0]),
        ],
    }
    steps = scenes.get(name, scenes["oneByOne"])
    while not program_stop.is_set():
        for ms, values in steps:
            if program_stop.is_set():
                break
            set_dpx620(address, values[:ch], ch, master)
            program_stop.wait(ms / 1000)


def pixe_rgb(red, green, blue, white=0, dim=255, channels=7):
    values = [0] * max(1, min(12, int(channels or 7)))
    if len(values) >= 1:
        values[0] = dim
    if len(values) >= 2:
        values[1] = red
    if len(values) >= 3:
        values[2] = green
    if len(values) >= 4:
        values[3] = blue
    if len(values) >= 5:
        values[4] = white
    return values


def run_pixezoom_program(name, addresses, channels, master):
    ch = max(1, min(12, int(channels or default_pixezoom_channels)))
    addresses = parse_addresses(",".join(str(a) for a in addresses))
    palettes = {
        "allWhite": [(900, [255] * ch)],
        "channelTest": [(300, [255 if i == n else 0 for i in range(ch)]) for n in range(ch)],
        "rgbChase": [
            (500, pixe_rgb(255, 0, 0, channels=ch)),
            (500, pixe_rgb(0, 255, 0, channels=ch)),
            (500, pixe_rgb(0, 0, 255, channels=ch)),
            (500, pixe_rgb(255, 120, 0, channels=ch)),
            (500, pixe_rgb(180, 0, 255, channels=ch)),
        ],
        "blueOrange": [
            (550, pixe_rgb(0, 70, 255, channels=ch)),
            (550, pixe_rgb(0, 130, 255, channels=ch)),
            (550, pixe_rgb(255, 80, 0, channels=ch)),
            (550, pixe_rgb(255, 140, 20, channels=ch)),
        ],
        "fixtureChase": [],
        "sparkle": [],
    }
    while not program_stop.is_set():
        if name == "fixtureChase":
            blank = [0] * ch
            active = pixe_rgb(255, 255, 255, white=80, channels=ch)
            for active_index, address in enumerate(addresses):
                if program_stop.is_set():
                    break
                with lock:
                    for each in addresses:
                        values = active if each == address else blank
                        for i, value in enumerate(values):
                            slot = each - 1 + i
                            if 0 <= slot < DMX_SLOTS:
                                dmx[slot] = (clamp(value) * clamp(master, 255)) // 255
                send_frame()
                program_stop.wait(0.35)
            continue
        if name == "sparkle":
            for step in range(12):
                if program_stop.is_set():
                    break
                values = [0] * ch
                if ch:
                    values[step % ch] = 255
                set_pixezoom(addresses, values, ch, master)
                program_stop.wait(0.18)
            continue
        steps = palettes.get(name, palettes["rgbChase"])
        for ms, values in steps:
            if program_stop.is_set():
                break
            set_pixezoom(addresses, values, ch, master)
            program_stop.wait(ms / 1000)


def status():
    with lock:
        first_16 = list(dmx[:16])
        dpx_start = max(0, min(DMX_SLOTS - 1, default_dpx_address - 1))
        dpx_view = list(dmx[dpx_start:dpx_start + 6])
        pixe_addresses = parse_addresses(default_pixezoom_addresses)
        pixe_view = {}
        for address in pixe_addresses:
            start = max(0, min(DMX_SLOTS - 1, address - 1))
            pixe_view[str(address)] = list(dmx[start:start + default_pixezoom_channels])
    return {
        "target": target_ip,
        "port": ARTNET_PORT,
        "universe": universe,
        "defaultDpxAddress": default_dpx_address,
        "defaultPixezoomAddresses": pixe_addresses,
        "defaultPixezoomChannels": default_pixezoom_channels,
        "framesSent": frames_sent,
        "lastAction": last_action,
        "programRunning": bool(program_thread and program_thread.is_alive()),
        "channels1to16": first_16,
        f"channels{default_dpx_address}to{default_dpx_address + 5}": dpx_view,
        "pixezoom": pixe_view,
    }


INDEX_TEMPLATE = r"""<!doctype html>
<html lang="pl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Art-Net DMX Tester</title>
<style>
:root{color-scheme:dark;--bg:#101418;--panel:#181f26;--line:#2b3742;--text:#e8edf2;--muted:#95a3b2;--green:#15945d;--red:#9d2432}
*{box-sizing:border-box}html,body{overflow-x:hidden}body{margin:0;background:var(--bg);color:var(--text);font:14px system-ui,Segoe UI,Arial,sans-serif}
header{padding:16px 20px;border-bottom:1px solid var(--line);display:flex;gap:14px;align-items:center;justify-content:space-between;flex-wrap:wrap}
h1{font-size:18px;margin:0}main{width:100%;max-width:1120px;margin:0 auto;padding:18px;display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,300px),1fr));gap:14px}
section{min-width:0;background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px}h2{font-size:15px;margin:0 0 12px}
label{display:grid;gap:5px;color:var(--muted);margin:8px 0}input,select,button{font:inherit;border-radius:6px;border:1px solid var(--line);background:#202a33;color:var(--text);padding:9px;min-height:42px;max-width:100%}
input[type=color]{height:42px;padding:2px}.row{display:flex;gap:8px;flex-wrap:wrap}.row>*{flex:1 1 130px;min-width:0}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,140px),1fr));gap:8px}
button{cursor:pointer}button.primary{background:var(--green);border-color:#1db875}button.bad{background:var(--red);border-color:#bd3344}
.metric{display:grid;grid-template-columns:120px minmax(0,1fr);gap:8px;padding:6px 0;border-bottom:1px solid #22303a}.metric span:first-child{color:var(--muted)}.metric span:last-child{overflow-wrap:anywhere}
.swatches{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}.swatches button{height:42px;color:#fff;text-shadow:0 1px 2px #000}
.channel{display:grid;grid-template-columns:44px minmax(0,1fr) 44px;gap:8px;align-items:center;margin:8px 0}.channel span{color:var(--muted)}.channel strong{text-align:right;font-variant-numeric:tabular-nums}
pre{white-space:pre-wrap;background:#0b0f13;border:1px solid var(--line);border-radius:6px;padding:10px;min-height:90px}
@media(max-width:640px){header{padding:12px}main{grid-template-columns:1fr;padding:10px;gap:10px}section{padding:12px}.row{display:grid;grid-template-columns:1fr 1fr}.swatches{grid-template-columns:repeat(2,1fr)}.metric{grid-template-columns:1fr}}
@media(max-width:360px){.row{grid-template-columns:1fr}main{padding:8px}section{padding:10px}}
</style>
</head>
<body>
<header><h1>Art-Net DMX Tester</h1><div id="top"></div></header>
<main>
<section>
<h2>Botex DPX-620 III Dimmer</h2>
<div class="grid">
<label>Adres DMX<input id="dpxAddr" type="number" min="1" max="512" value="__DPX_ADDRESS__"></label>
<label>Tryb kanałów<select id="dpxChannels" onchange="renderDpxChannels()"><option value="6">6 kanałów</option><option value="4">4 kanały</option></select></label>
</div>
<label>Master<input id="dpxMaster" type="range" min="0" max="255" value="255" oninput="renderDpxChannels()"></label>
<div id="dpxChannelsBox"></div>
<div class="row">
<button class="primary" onclick="sendDpx()">Wyślij dimmer</button>
<button onclick="dpxScene('all')">Wszystkie ON</button>
<button onclick="dpxScene('half')">Wszystkie 50%</button>
<button class="bad" onclick="blackout()">Blackout</button>
</div>
<h2 style="margin-top:18px">Programy DPX</h2>
<div class="row">
<button onclick="dpxProgram('oneByOne')">Test kanałów</button>
<button onclick="dpxProgram('softWave')">Płynna fala</button>
<button onclick="dpxProgram('alternate')">Parzyste / nieparzyste</button>
<button onclick="dpxProgram('pulse')">Puls master</button>
<button onclick="dpxProgram('chasePairs')">Chase parami</button>
<button class="bad" onclick="stopProgram()">Stop</button>
</div>
<pre id="dpxInfo"></pre>
</section>
<section>
<h2>4x Pixezoom / Generic DMX</h2>
<label>Adresy urządzeń CSV<input id="pixeAddresses" value="__PIXEZOOM_ADDRESSES__"></label>
<div class="grid">
<label>Kanały na urządzenie<select id="pixeChannels" onchange="renderPixeChannels()"><option value="7">7 kanałów</option><option value="6">6 kanałów</option><option value="5">5 kanałów</option><option value="11">11 kanałów</option></select></label>
<label>Master<input id="pixeMaster" type="range" min="0" max="255" value="255" oninput="renderPixeChannels()"></label>
</div>
<div id="pixeChannelsBox"></div>
<div class="row">
<button class="primary" onclick="sendPixe()">Wyślij Pixezoom</button>
<button onclick="pixePreset('allWhite')">Wszystkie 100%</button>
<button onclick="pixePreset('rgbRed')">Red</button>
<button onclick="pixePreset('rgbBlue')">Blue</button>
<button class="bad" onclick="blackout()">Blackout</button>
</div>
<h2 style="margin-top:18px">Demo Pixezoom</h2>
<div class="row">
<button onclick="pixeProgram('channelTest')">Test kanałów</button>
<button onclick="pixeProgram('fixtureChase')">Chase lamp</button>
<button onclick="pixeProgram('rgbChase')">RGB chase</button>
<button onclick="pixeProgram('blueOrange')">Blue / orange</button>
<button onclick="pixeProgram('sparkle')">Sparkle kanałów</button>
<button class="bad" onclick="stopProgram()">Stop</button>
</div>
<pre id="pixeInfo"></pre>
</section>
<section>
<h2>SHE-FS403A 7CH</h2>
<div class="grid">
<label>Adres DMX<input id="addr" type="number" min="1" max="512" value="1"></label>
<label>Kolor<input id="color" type="color" value="#ff0000" oninput="fromColor()"></label>
</div>
<label>Dimmer<input id="dim" type="range" min="0" max="255" value="255" oninput="showVals()"></label>
<label>Red<input id="r" type="range" min="0" max="255" value="255" oninput="showVals()"></label>
<label>Green<input id="g" type="range" min="0" max="255" value="0" oninput="showVals()"></label>
<label>Blue<input id="b" type="range" min="0" max="255" value="0" oninput="showVals()"></label>
<label>White<input id="w" type="range" min="0" max="255" value="0" oninput="showVals()"></label>
<label>CH6 mode<input id="mode" type="range" min="0" max="255" value="0" oninput="showVals()"></label>
<label>CH7 strobe/speed<input id="strobe" type="range" min="0" max="255" value="0" oninput="showVals()"></label>
<div class="row">
<button class="primary" onclick="sendFixture()">Wyślij Art-Net</button>
<button class="bad" onclick="blackout()">Blackout</button>
</div>
<pre id="vals"></pre>
</section>
<section>
<h2>Kolory i szybkie testy</h2>
<div class="swatches">
<button style="background:#f00" onclick="preset(255,0,0,0)">Red</button>
<button style="background:#0d0" onclick="preset(0,255,0,0)">Green</button>
<button style="background:#04f" onclick="preset(0,0,255,0)">Blue</button>
<button style="background:#fff;color:#111;text-shadow:none" onclick="preset(0,0,0,255)">White</button>
<button style="background:#00dfff" onclick="preset(0,180,255,0)">Cyan</button>
<button style="background:#ff7a00" onclick="preset(255,90,0,0)">Orange</button>
<button style="background:#fb00ff" onclick="preset(255,0,220,0)">Magenta</button>
<button style="background:#ffd04a;color:#111;text-shadow:none" onclick="preset(255,170,30,30)">Warm</button>
</div>
<h2 style="margin-top:18px">Programy Art-Net</h2>
<div class="row">
<button onclick="program('blueShades')">Blue shades</button>
<button onclick="program('orangeShades')">Orange shades</button>
<button onclick="program('rgb')">RGBW chase</button>
<button onclick="program('strobe')">Strobe test</button>
<button class="bad" onclick="stopProgram()">Stop</button>
</div>
</section>
<section>
<h2>Raw DMX</h2>
<label>Adres startowy<input id="rawAddr" type="number" min="1" max="512" value="1"></label>
<label>Wartości CSV<input id="rawValues" value="255,255,0,0,0,0,0"></label>
<div class="row"><button class="primary" onclick="sendRaw()">Wyślij zakres</button></div>
<pre>Przykład dla 7CH:
255,255,0,0,0,0,0 = czerwony
255,0,0,255,0,0,0 = niebieski
255,255,80,0,0,0,0 = pomarańczowy</pre>
</section>
<section>
<h2>Status</h2>
<div id="status"></div>
</section>
</main>
<script>
function q(id){return document.getElementById(id)}
async function post(path,data){await fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});refresh()}
function dpxCh(){return +q('dpxChannels').value}
function dpxValues(){let out=[];for(let i=1;i<=dpxCh();i++)out.push(+q('dpxCh'+i).value||0);return out}
function renderDpxChannels(){let count=dpxCh(), html='';for(let i=1;i<=count;i++){let old=q('dpxCh'+i)?.value||0;html+=`<div class="channel"><span>CH${i}</span><input id="dpxCh${i}" type="range" min="0" max="255" value="${old}" oninput="renderDpxChannels()"><strong>${old}</strong></div>`}q('dpxChannelsBox').innerHTML=html;let start=+q('dpxAddr').value,end=start+count-1;q('dpxInfo').textContent=`DPX-620 III Start mode: adres ${start}, kanały ${start}-${end}. Master ${q('dpxMaster').value}/255.`}
function sendDpx(){post('/api/dpx',{address:+q('dpxAddr').value,channels:dpxCh(),master:+q('dpxMaster').value,values:dpxValues()})}
function dpxScene(name){let val=name==='all'?255:128;for(let i=1;i<=dpxCh();i++)q('dpxCh'+i).value=val;renderDpxChannels();sendDpx()}
function dpxProgram(name){post('/api/dpx/program',{name,address:+q('dpxAddr').value,channels:dpxCh(),master:+q('dpxMaster').value})}
function pixeCh(){return +q('pixeChannels').value}
function pixeValues(){let out=[];for(let i=1;i<=pixeCh();i++)out.push(+q('pixeCh'+i).value||0);return out}
function renderPixeChannels(){let count=pixeCh(), html='';for(let i=1;i<=count;i++){let old=q('pixeCh'+i)?.value||0;html+=`<div class="channel"><span>CH${i}</span><input id="pixeCh${i}" type="range" min="0" max="255" value="${old}" oninput="renderPixeChannels()"><strong>${old}</strong></div>`}q('pixeChannelsBox').innerHTML=html;let addresses=q('pixeAddresses').value.split(',').map(x=>+x.trim()).filter(Boolean);let ranges=addresses.map(a=>`${a}-${a+count-1}`).join(', ');q('pixeInfo').textContent=`Adresy: ${addresses.join(', ')}. Zakresy DMX: ${ranges}. Jeśli urządzenia są naprawdę 6CH, ustaw 6 kanałów; odstępy 100,107,114 wyglądają jak 7CH.`}
function pixePayload(name){let ch=pixeCh(), v=new Array(ch).fill(0);if(name==='allWhite'){v.fill(255)}else if(name==='rgbRed'){if(ch>0)v[0]=255;if(ch>1)v[1]=255}else if(name==='rgbBlue'){if(ch>0)v[0]=255;if(ch>3)v[3]=255;else v[ch-1]=255}return v}
function sendPixe(){post('/api/pixezoom',{addresses:q('pixeAddresses').value,channels:pixeCh(),master:+q('pixeMaster').value,values:pixeValues()})}
function pixePreset(name){let values=pixePayload(name);for(let i=1;i<=pixeCh();i++)q('pixeCh'+i).value=values[i-1]||0;renderPixeChannels();sendPixe()}
function pixeProgram(name){post('/api/pixezoom/program',{name,addresses:q('pixeAddresses').value,channels:pixeCh(),master:+q('pixeMaster').value})}
function vals(){return {address:+q('addr').value,dim:+q('dim').value,r:+q('r').value,g:+q('g').value,b:+q('b').value,w:+q('w').value,mode:+q('mode').value,strobe:+q('strobe').value}}
function showVals(){let v=vals();q('vals').textContent=`CH1 dim=${v.dim}\nCH2 red=${v.r}\nCH3 green=${v.g}\nCH4 blue=${v.b}\nCH5 white=${v.w}\nCH6 mode=${v.mode}\nCH7 strobe/speed=${v.strobe}`}
function fromColor(){let c=q('color').value;q('r').value=parseInt(c.slice(1,3),16);q('g').value=parseInt(c.slice(3,5),16);q('b').value=parseInt(c.slice(5,7),16);q('w').value=0;showVals()}
function preset(r,g,b,w){q('dim').value=255;q('r').value=r;q('g').value=g;q('b').value=b;q('w').value=w;q('mode').value=0;q('strobe').value=0;q('color').value='#'+[r,g,b].map(x=>x.toString(16).padStart(2,'0')).join('');showVals();sendFixture()}
function sendFixture(){post('/api/fixture',vals())}
function blackout(){post('/api/blackout',{})}
function program(name){post('/api/program',{name,address:+q('addr').value})}
function stopProgram(){post('/api/program/stop',{})}
function sendRaw(){post('/api/range',{address:+q('rawAddr').value,values:q('rawValues').value.split(',').map(v=>+v.trim()||0)})}
async function refresh(){let s=await (await fetch('/api/status')).json();q('top').textContent=`${s.target}:${s.port} universe ${s.universe}`;q('status').innerHTML=Object.entries(s).map(([k,v])=>`<div class="metric"><span>${k}</span><span>${Array.isArray(v)?v.join(', '):v}</span></div>`).join('')}
renderDpxChannels();renderPixeChannels();showVals();refresh();setInterval(refresh,1500)
</script>
</body>
</html>
"""

INDEX = (
    INDEX_TEMPLATE
    .replace("__DPX_ADDRESS__", str(default_dpx_address))
    .replace("__PIXEZOOM_ADDRESSES__", default_pixezoom_addresses)
)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print("%s - %s" % (self.address_string(), fmt % args))

    def read_json(self):
        size = int(self.headers.get("Content-Length", "0"))
        if size <= 0:
            return {}
        return json.loads(self.rfile.read(size).decode("utf-8"))

    def send_json(self, value):
        body = json.dumps(value).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/status":
            self.send_json(status())
            return
        body = INDEX.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        global last_action
        path = urlparse(self.path).path
        data = self.read_json()
        if path == "/api/fixture":
            stop_program()
            last_action = "fixture"
            set_she_fs403a(
                int(data.get("address", 1)),
                data.get("dim", 0),
                data.get("r", 0),
                data.get("g", 0),
                data.get("b", 0),
                data.get("w", 0),
                data.get("mode", 0),
                data.get("strobe", 0),
            )
            self.send_json(status())
        elif path == "/api/dpx":
            stop_program()
            last_action = "dpx"
            set_dpx620(
                int(data.get("address", default_dpx_address)),
                data.get("values", []),
                data.get("channels", 6),
                data.get("master", 255),
            )
            self.send_json(status())
        elif path == "/api/pixezoom":
            stop_program()
            last_action = "pixezoom"
            set_pixezoom(
                parse_addresses(data.get("addresses", default_pixezoom_addresses)),
                data.get("values", []),
                data.get("channels", default_pixezoom_channels),
                data.get("master", 255),
            )
            self.send_json(status())
        elif path == "/api/range":
            stop_program()
            last_action = "range"
            set_range(int(data.get("address", 1)), data.get("values", []))
            self.send_json(status())
        elif path == "/api/blackout":
            stop_program()
            last_action = "blackout"
            blackout()
            self.send_json(status())
        elif path == "/api/program":
            start_program(str(data.get("name", "rgb")), int(data.get("address", 1)))
            self.send_json(status())
        elif path == "/api/dpx/program":
            start_dpx_program(
                str(data.get("name", "oneByOne")),
                int(data.get("address", default_dpx_address)),
                int(data.get("channels", 6)),
                int(data.get("master", 255)),
            )
            self.send_json(status())
        elif path == "/api/pixezoom/program":
            start_pixezoom_program(
                str(data.get("name", "rgbChase")),
                parse_addresses(data.get("addresses", default_pixezoom_addresses)),
                int(data.get("channels", default_pixezoom_channels)),
                int(data.get("master", 255)),
            )
            self.send_json(status())
        elif path == "/api/program/stop":
            stop_program()
            last_action = "program:stop"
            self.send_json(status())
        else:
            self.send_error(404)


if __name__ == "__main__":
    print(f"Art-Net target: {target_ip}:{ARTNET_PORT}, universe {universe}")
    print(f"HTTP panel: http://127.0.0.1:{http_port}/")
    ThreadingHTTPServer(("0.0.0.0", http_port), Handler).serve_forever()
