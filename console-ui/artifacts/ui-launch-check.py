# Drives the real Prism Console UI (remote-debugging port 9223, isolated display) through a
# CrazyCraft launch by clicking, records the launch overlay states and screenshots, then stops the game.
import base64, json, sys, time, urllib.request
from pathlib import Path
import websocket
sys.path.insert(0, str(Path(__file__).parent))
from virtual_pad import VirtualPad

out = Path('/home/deck/Applications/prism-console-test-results/launch')
out.mkdir(parents=True, exist_ok=True)
instance = Path('/home/deck/.var/app/io.github.CoolFreeze23.PrismConsole/data/PrismLauncher/instances/CrazyCraft5/.minecraft')
failures = []

# The launch must run under Gaming Mode's environment and over a non-ASCII file name: Steam starts tiles with
# LC_ALL=C, and CrazyCraft's pre-launch loader (Java) once exited with code 1 on a pack named with section signs.
odd = [p.name for p in (instance / 'resourcepacks').iterdir() if not p.name.isascii()]
print('Non-ASCII names the loader checks:', odd[:3] or 'NONE')
if not odd: failures.append('no non-ASCII file in the CrazyCraft instance: the locale case is no longer covered')

def process_env(pid):
    return dict(line.split('=', 1) for line in Path(f'/proc/{pid}/environ').read_bytes().decode(errors='replace').split('\0') if '=' in line)
def environment_report():
    import subprocess
    app = subprocess.run(['pgrep', '-f', '^/app/bin/prismlauncher --dir'], capture_output=True, text=True).stdout.split()
    host = subprocess.run(['pgrep', '-f', '^flatpak run .*QTWEBENGINE_REMOTE_DEBUGGING=127.0.0.1:9223 .*io.github.CoolFreeze23.PrismConsole'], capture_output=True, text=True).stdout.split()
    if not app or not host: failures.append('could not find the app or its launcher process'); return
    started, inside = process_env(host[0]), process_env(app[0])
    print(f"Started with LC_ALL={started.get('LC_ALL', '-')} SteamGamepadUI={started.get('SteamGamepadUI', '-')} LD_PRELOAD={'set' if started.get('LD_PRELOAD') else '-'}; "
          f"inside the app LC_ALL={inside.get('LC_ALL', '-')} LC_CTYPE={inside.get('LC_CTYPE', '-')}")
    if started.get('LC_ALL') != 'C': failures.append('not started with Gaming Mode environment (LC_ALL=C)')

for _ in range(120):
    try:
        targets = json.load(urllib.request.urlopen('http://127.0.0.1:9223/json'))
        target = next(t for t in targets if t.get('url', '').startswith('qrc:/console/'))
        break
    except Exception:
        time.sleep(1)
else:
    sys.exit('UI never exposed its debug target')
ws = websocket.create_connection(target['webSocketDebuggerUrl'], timeout=60, suppress_origin=True)
serial = 0

def command(method, params=None):
    global serial
    serial += 1
    ws.send(json.dumps({'id': serial, 'method': method, 'params': params or {}}))
    while True:
        msg = json.loads(ws.recv())
        if msg.get('id') == serial:
            if 'error' in msg: raise RuntimeError(msg['error'])
            return msg['result']

def js(expression):
    result = command('Runtime.evaluate', {'expression': expression, 'awaitPromise': True, 'returnByValue': True})
    if 'exceptionDetails' in result: raise RuntimeError(result['exceptionDetails'])
    return result['result'].get('value')

def ui_shot(name):
    # Chromium cannot capture a page while the launcher window is hidden for the game.
    if js('document.visibilityState') == 'hidden':
        print(f'(skipped {name}: launcher hidden)'); return
    (out / f'{name}.png').write_bytes(base64.b64decode(command('Page.captureScreenshot', {'format': 'png'})['data']))

def display_shot(name):
    # Read the display number here: the virtual display writes it only once it is up.
    from PIL import ImageGrab
    display = Path('/home/deck/Applications/prism-console-pack-display').read_text().strip()
    ImageGrab.grab(xdisplay=':' + display).save(out / f'{name}.png')

def press(action):
    return js(f"(() => {{ const b = document.querySelector('[data-action={action}]:not(:disabled)'); b?.click(); return !!b }})()")

def click(text):
    return js(f"(() => {{ const b = [...document.querySelectorAll('button')].find(b => b.textContent.trim().includes({json.dumps(text)}) && !b.disabled); if (b) b.click(); return !!b }})()")

def overlay():
    return js("(() => { const o = document.querySelector('.launch-overlay'); return o ? { failed: o.classList.contains('failed'), step: o.querySelector('[aria-current=step]')?.textContent.trim() || '', status: o.querySelector('.launch-status')?.textContent.trim(), progress: o.querySelector('.launch-progress span')?.style.width || '', stall: o.querySelector('.launch-stall')?.textContent.trim() || '' } : null })()")

for _ in range(60):
    if js("document.querySelector('.bp-container')?.dataset.connected === 'true'"): break
    time.sleep(0.5)
else:
    sys.exit('UI never connected to Prism')
time.sleep(2)
environment_report()
if js("!!document.querySelector('[role=dialog] #dialog-title')?.textContent.includes('Welcome')"):
    print('Onboarding shown; keeping existing settings:', click('Keep current settings')); time.sleep(1)
# Picking a pack is one tap; a tap on the pack that is already picked would play it.
print('Selected CrazyCraft card:', js("(() => { const c = [...document.querySelectorAll('.instance-card')].find(c => c.textContent.includes('CrazyCraft')); if (c && !c.classList.contains('selected')) c.click(); return !!c })()"))
time.sleep(1)
ui_shot('1-library')
pad = VirtualPad()
time.sleep(4)  # the chord watcher scans for new pads every 3 s
# Sound probe: every synthesized note is recorded with the page's visibility when it started.
print('Sound probe installed:', js("(() => { window.__notes = []; const make = AudioContext.prototype.createOscillator; AudioContext.prototype.createOscillator = function () { window.__notes.push(document.visibilityState); return make.call(this) }; return true })()"))
pressed_at = time.time()
print('Pressed Play:', press('play'))

seen, started, last_shot = [], time.time(), 0
result = 'timeout'
while time.time() - started < 900:
    state = overlay()
    running = js("document.querySelector('.running-banner') !== null")
    prompt = js("document.querySelector('.dialog-compact #dialog-title')?.textContent || null")
    if prompt == 'Offline player name' and 'offline' not in seen:
        # Microsoft/Mojang sign-in unreachable: Prism offers offline play. Answer as a player would and continue.
        seen.append('offline'); ui_shot('prompt-offline')
        print(f"[{int(time.time() - started):4d}s] prompt: {prompt} -> Continue:", js("(() => { const b = [...document.querySelectorAll('.dialog-compact button')].find(b => b.dataset.choice === 'Continue' || b.textContent.trim() === 'Continue'); b?.click(); return !!b })()"))
        time.sleep(1); continue
    if prompt:
        ui_shot('prompt'); result = f'prompt: {prompt}'; break
    if state and state['status'] and (not seen or seen[-1] != (state['step'], state['status'])):
        seen.append((state['step'], state['status'])); print(f"[{int(time.time() - started):4d}s] {state['step']} | {state['status']} {state['progress']}")
    if state and state['stall'] and 'stall' not in seen:
        seen.append('stall'); print(f"[{int(time.time() - started):4d}s] stall message: {state['stall']}")
    if state and time.time() - last_shot > 20:
        ui_shot(f"2-overlay-{int(time.time() - started):04d}s"); last_shot = time.time()
    if state and state['failed']:
        ui_shot('3-failed'); result = 'failed'; break
    if running and not state:
        result = 'running'; break
    time.sleep(1)
print('Launch result:', result, f'after {int(time.time() - started)}s')
if result != 'running':
    failures.append(f'launch {result}')
    print('Launch screen said:', js("document.querySelector('.launch-overlay')?.innerText.slice(0, 600) || null"))

if result == 'running':
    time.sleep(1)
    print('Launcher page after hand-off:', js('document.visibilityState'))
    print('Notes played by Play (launch sound):', js('window.__notes.length'))
    # Moving focus and a toast while the game has the screen must stay silent.
    js("(() => { const b = [...document.querySelectorAll('main button:not(:disabled)')]; b[0]?.focus(); b[1]?.focus(); window.dispatchEvent(new Event('focusin')); return true })()")
    hidden_notes = js("window.__notes.filter(v => v === 'hidden').length")
    print('Notes while the game had the screen:', hidden_notes, '(PASS)' if hidden_notes == 0 else '(FAIL)')
    if hidden_notes: failures.append('sounds played over the game')
    display_shot('3-handoff-display')
    # Give NeoForge time to finish loading mods and reach the title screen, sampling the game window.
    for i in range(18):
        time.sleep(20); display_shot(f'4-game-{(i + 1) * 20:03d}s')
        # Only a log rewritten after Play was pressed counts; the previous run's latest.log has the same markers.
        log_path = Path('/home/deck/.var/app/io.github.CoolFreeze23.PrismConsole/data/PrismLauncher/instances/CrazyCraft5/.minecraft/logs/latest.log')
        fresh = log_path.exists() and log_path.stat().st_mtime > pressed_at
        if fresh and 'Sound engine started' in log_path.read_text(errors='replace'):
            print(f'Title-screen markers in latest.log after ~{(i + 1) * 20}s'); time.sleep(15); display_shot('5-title'); break
    # View + Menu from a virtual pad: the launcher comes forward over the game, then hands the screen back.
    pad.chord()
    time.sleep(2)
    print('After chord, launcher page:', js('document.visibilityState'), '| banner:', js("document.querySelector('.running-banner')?.textContent.trim() || null"))
    before = js('window.__notes.length')
    js("(() => { const b = [...document.querySelectorAll('main button:not(:disabled)')]; b[0]?.focus(); b[1]?.focus(); return true })()")
    time.sleep(0.3)
    print('Focus move over the game with the launcher shown played notes:', js('window.__notes.length') - before)
    ui_shot('5b-launcher-over-game'); display_shot('5b-display-launcher')
    pad.chord()
    time.sleep(2)
    print('After second chord, launcher page:', js('document.visibilityState'))
    display_shot('5c-display-back-to-game')
    print('Stopping through the UI:', press('stop-game')); time.sleep(1); ui_shot('6-stop-confirm')
    print('Confirmed stop:', press('confirm-stop'))
    time.sleep(2)
    # Prism's own launch controller may ask once more before killing the process.
    if js("document.querySelector('.dialog-compact #dialog-title')?.textContent || ''") == 'Cancel launch or stop game?':
        ui_shot('6b-controller-prompt')
        print('Answered controller prompt:', js("(() => { const b = [...document.querySelectorAll('.dialog-compact button')].find(b => b.textContent.trim() === 'Stop'); b?.click(); return !!b })()"))
    time.sleep(8); ui_shot('7-after-stop')
    print('Still running after stop:', js("document.querySelector('[data-action=stop-game]') !== null"))
    for _ in range(15):
        if js('document.visibilityState') == 'visible': break
        time.sleep(1)
    print('Launcher page after stop:', js('document.visibilityState'))
    ui_shot('9-launcher-back')
    display_shot('8-after-stop-display')
print('Sound notes by visibility:', js("JSON.stringify(window.__notes.reduce((a, v) => (a[v] = (a[v] || 0) + 1, a), {}))"))
ws.close()
pad.close()
print('LAUNCH CHECK', 'FAILED: ' + '; '.join(failures) if failures else 'PASSED')
sys.exit(1 if failures else 0)
