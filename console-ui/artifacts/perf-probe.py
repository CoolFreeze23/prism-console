# Measures Prism Console on the handheld (run-pack-check display, debug port 9223): CPU and RSS of the launcher
# and its web engine, the page's JS heap, and the size of the polled replies, idle and with a long modpack list.
import json, subprocess, sys, time, urllib.request
import websocket

def processes():
    # Only this Prism Console runs during the probe, so the launcher and its web engine are matched by name.
    out = subprocess.run(['ps', '-eo', 'pid=,rss=,comm='], capture_output=True, text=True).stdout
    rows = [line.split(None, 2) for line in out.splitlines()]
    return [(int(pid), int(rss), comm) for pid, rss, comm in rows if comm in ('prismlauncher', 'QtWebEngineProc')]

def cpu_seconds(pids):
    total = 0
    for pid in pids:
        try:
            fields = open(f'/proc/{pid}/stat').read().rsplit(')', 1)[1].split()
            total += int(fields[11]) + int(fields[12])
        except FileNotFoundError:
            pass
    return total / 100.0  # clock ticks

for _ in range(120):
    try:
        target = next(t for t in json.load(urllib.request.urlopen('http://127.0.0.1:9223/json')) if t.get('url', '').startswith('qrc:/console/'))
        break
    except Exception:
        time.sleep(1)
ws = websocket.create_connection(target['webSocketDebuggerUrl'], timeout=60, suppress_origin=True)
serial = 0
def command(method, params=None):
    global serial
    serial += 1
    ws.send(json.dumps({'id': serial, 'method': method, 'params': params or {}}))
    while True:
        msg = json.loads(ws.recv())
        if msg.get('id') == serial:
            return msg.get('result', {})
def js(expression):
    return command('Runtime.evaluate', {'expression': expression, 'awaitPromise': True, 'returnByValue': True})['result'].get('value')
command('Performance.enable')
for _ in range(60):
    if js("document.querySelector('.bp-container')?.dataset.connected === 'true'"): break
    time.sleep(0.5)
def sample(label, seconds=30):
    procs = processes()
    pids = [p[0] for p in procs]
    before = cpu_seconds(pids)
    time.sleep(seconds)
    used = cpu_seconds(pids) - before
    heap = {m['name']: m['value'] for m in command('Performance.getMetrics')['metrics']}.get('JSHeapUsedSize', 0)
    rss = sum(p[1] for p in processes()) / 1024
    print(f"{label}: CPU {used / seconds * 100:.1f}% of one core, RSS {rss:.0f} MB ({len(procs)} processes), JS heap {heap / 1e6:.1f} MB")

def reply_sizes():
    # A second QWebChannel on the page's transport takes over its replies, so this runs last.
    js('new Promise(r => new QWebChannel(qt.webChannelTransport, c => { window.probe = c.objects.prism; r(true) }))')
    size = lambda method: js(f"new Promise(r => probe.request({json.dumps(method)}, {{}}, x => r(JSON.stringify(x).length)))")
    print(f"reply sizes: snapshot {size('snapshot') / 1024:.1f} KB, modpacks {size('modpacks') / 1024:.1f} KB")

time.sleep(5)
sample('Idle on the library')
js("document.querySelector('[data-action=browse-modpacks]')?.click()")
for page in range(4):  # about 100 packs, as a long scroll would load
    for _ in range(80):
        if js("document.querySelectorAll('.modpack-card').length") >= 24 * (page + 1) and not js("!!document.querySelector('[data-action=load-more]')?.disabled"): break
        time.sleep(0.25)
    if page < 3:
        js("document.querySelector('[data-action=load-more]')?.click()")
time.sleep(4)
cards = js("document.querySelectorAll('.modpack-card').length")
icons = js("[...document.querySelectorAll('.modpack-card img')].filter(i => i.src.startsWith('data:image/png')).length")
print(f"modpack list: {cards} cards, {icons} icons shown")
sample('Idle with the modpack list open')
for _ in range(3):
    if not js("!!document.querySelector('.modal-scrim')"): break
    js("document.querySelector('[data-action=close-dialog]')?.click()"); time.sleep(0.5)
time.sleep(1)
sample('Back on the library', 20)
reply_sizes()
