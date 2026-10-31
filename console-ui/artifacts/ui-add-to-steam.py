# Presses Configure -> "Add to Steam" for CrazyCraft in the real Prism Console UI (harness debug port) and
# reports the resulting toast and the tile ID Prism recorded.
import json, sys, time, urllib.request
from pathlib import Path
import websocket
sys.path.insert(0, str(Path(__file__).parent))
from harness_port import debug_targets

for _ in range(120):
    try:
        target = next(t for t in json.load(urllib.request.urlopen(debug_targets())) if t.get('url', '').startswith('qrc:/console/'))
        break
    except Exception:
        time.sleep(1)
else:
    sys.exit('UI never exposed its debug target')
ws = websocket.create_connection(target['webSocketDebuggerUrl'], timeout=60, suppress_origin=True)
serial = 0

def js(expression):
    global serial
    serial += 1
    ws.send(json.dumps({'id': serial, 'method': 'Runtime.evaluate', 'params': {'expression': expression, 'awaitPromise': True, 'returnByValue': True}}))
    while True:
        msg = json.loads(ws.recv())
        if msg.get('id') == serial:
            return msg['result']['result'].get('value')

def click(text):
    return js(f"(() => {{ const b = [...document.querySelectorAll('button')].find(b => b.textContent.trim() === {json.dumps(text)} && !b.disabled); b?.click(); return !!b }})()")

for _ in range(60):
    if js("document.querySelector('.bp-container')?.dataset.connected === 'true'"): break
    time.sleep(0.5)
time.sleep(2)
js("[...document.querySelectorAll('.instance-card')].find(c => c.textContent.includes('CrazyCraft'))?.click()")
time.sleep(0.5)
print('Opened Configure:', js("(() => { const b = document.querySelector('[data-action=configure]'); b?.click(); return !!b })()"))
time.sleep(1)
print('Pressed Add to Steam:', js("(() => { const b = document.querySelector('[data-action=add-to-steam]:not(:disabled)'); b?.click(); return !!b })()"))
for _ in range(60):
    toasts = js("[...document.querySelectorAll('.toast span')].map(t => t.textContent)")
    if toasts: print('Toast:', toasts); break
    time.sleep(0.5)
time.sleep(2.5)  # one snapshot refresh so the recorded tile ID reaches the page
print('Button now reads:', js("[...document.querySelectorAll('.dialog-body button')].map(b => b.textContent.trim()).find(t => /Steam/.test(t))"))
