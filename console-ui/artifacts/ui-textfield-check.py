# Handheld check with a virtual pad (stick + A/B) on the real Prism Console (run-pack-check.sh, debug port 9223):
# the modpack browser starts on a result, a text field does not trap the stick, A starts typing (the stick then
# stays for Steam's keyboard), B stops typing, and the search filters reach Modrinth.
import json, sys, time, urllib.request
from pathlib import Path
import websocket
sys.path.insert(0, str(Path(__file__).parent))
from virtual_pad import VirtualPad, BTN_SOUTH, BTN_EAST, ABS_Y

for _ in range(120):
    try:
        target = next(t for t in json.load(urllib.request.urlopen('http://127.0.0.1:9223/json')) if t.get('url', '').startswith('qrc:/console/'))
        break
    except Exception:
        time.sleep(1)
else:
    sys.exit('UI never exposed its debug target')
ws = websocket.create_connection(target['webSocketDebuggerUrl'], timeout=30, suppress_origin=True)
serial = 0
def js(expression):
    global serial
    serial += 1
    ws.send(json.dumps({'id': serial, 'method': 'Runtime.evaluate', 'params': {'expression': expression, 'awaitPromise': True, 'returnByValue': True}}))
    while True:
        reply = json.loads(ws.recv())
        if reply.get('id') == serial:
            return reply['result']['result'].get('value')
def wait(expression, what, seconds=30):
    for _ in range(seconds * 4):
        if js(expression): return
        time.sleep(0.25)
    raise AssertionError(f'Timed out: {what}')

failures = []
def check(ok, label):
    print(('PASS: ' if ok else 'FAIL: ') + label)
    if not ok: failures.append(label)

wait("document.querySelector('.bp-container')?.dataset.connected === 'true'", 'UI connected')
time.sleep(2)
pad = VirtualPad('Prism Console stick pad', stick=True)
time.sleep(4)  # Chromium picks up new input devices through udev after a moment
for _ in range(2):  # and exposes a pad only after one of its buttons is pressed (B with nothing open does nothing)
    pad.tap(BTN_EAST); time.sleep(1)
pads = js("[...navigator.getGamepads()].filter(Boolean).map(g => g.id.slice(0, 40))")
print('Pads the page sees:', pads)
# Steam hides controllers from apps it did not start (a test pad included) unless a Steam-launched app is
# running; the stick checks then cannot run and are reported as skipped, not passed.
pad_checks = bool(pads)
if '--diagnose' in sys.argv:
    pads = "JSON.stringify([...navigator.getGamepads()].filter(Boolean).map(g => [g.id.slice(0, 30), g.mapping, g.axes.slice(0, 2).map(a => a.toFixed(2)), g.buttons.slice(0, 4).map(b => +b.pressed)]))"
    print('idle:', js(pads))
    pad._emit(3, ABS_Y, 32767); pad._emit(0, 0, 0); time.sleep(0.3)
    print('stick down:', js(pads))
    pad._emit(3, ABS_Y, 0); pad._emit(0, 0, 0)
    pad.buttons((BTN_SOUTH,), True); time.sleep(0.2)
    print('A held:', js(pads))
    pad.buttons((BTN_SOUTH,), False)

js("document.querySelector('[data-action=browse-modpacks]').click(); true")
wait("!!document.querySelector('.modpack-card')", 'modpack results', 60)
time.sleep(1)
check(js("!!document.activeElement?.closest('.modpack-card')"), 'the browser starts on a modpack, not the search box')
field = "document.activeElement?.matches('.modpack-search input')"

if not pad_checks:
    print('SKIPPED: stick and A/B checks (no pad visible to the page; see above)')
js("document.querySelector('.modpack-search input').focus(); true")
pad.push(ABS_Y, 1); time.sleep(0.4)
pad_checks and check(not js(field), 'the stick moves on from the search box')

js("document.querySelector('.modpack-search input').focus(); true")
pad.tap(BTN_SOUTH); time.sleep(0.4)  # A: start typing
pad.push(ABS_Y, 1); time.sleep(0.4)
pad_checks and check(bool(js(field)), 'while typing, the stick stays with the search box (Steam keyboard)')
pad.tap(BTN_EAST); time.sleep(0.4)   # B: stop typing
pad_checks and check(bool(js(field)), 'B stops typing and keeps the search box selected')
pad.push(ABS_Y, 1); time.sleep(0.4)
pad_checks and check(not js(field), 'after B the stick moves on again')
pad_checks and check(bool(js("!!document.querySelector('.modpacks')")), 'B while typing did not leave the Browse tab')

js("document.querySelector('[data-loader=neoforge]').click(); true")
wait("document.querySelector('[data-loader=neoforge]')?.getAttribute('aria-checked') === 'true' && !document.querySelector('.modpack-results [role=status]') && !!document.querySelector('.modpack-card')", 'NeoForge results', 60)
check(not js("!!document.querySelector('.modpack-error')"), 'the NeoForge filter returns results from Modrinth')
js("document.querySelector('.bp-nav-btn').click(); true")
pad.close()
ws.close()
print('TEXT FIELD CHECK', 'FAILED: ' + '; '.join(failures) if failures else 'PASSED')
sys.exit(1 if failures else 0)
