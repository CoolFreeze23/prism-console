# Stop check on the handheld: plays a copy of a CrazyCraft world through the real Prism Console UI (harness debug
# port, isolated display), then presses Stop. Passes when the world files were written after Stop, the game's Java
# and the pack's Crash Assistant are gone within 25 s, and the launcher reports a normal close rather than a crash.
# The save is judged by the files: the game's logger shuts down with the JVM before the save lines are written.
# The world copy is made and removed by the runner; its folder name comes as the first argument.
import json, subprocess, sys, time, urllib.request
from pathlib import Path
import websocket
sys.path.insert(0, str(Path(__file__).parent))
from harness_port import debug_targets

world = sys.argv[1] if len(sys.argv) > 1 else 'Prism stop check'
minecraft = Path('/home/deck/.var/app/io.github.CoolFreeze23.PrismConsole/data/PrismLauncher/instances/CrazyCraft5/.minecraft')
level = minecraft / 'saves' / world / 'level.dat'
log_path = minecraft / 'logs' / 'latest.log'
failures = []
if not level.exists(): sys.exit(f'no world copy at {level.parent}')

for _ in range(90):
    try:
        targets = json.load(urllib.request.urlopen(debug_targets()))
        target = next(t for t in targets if t.get('url', '').startswith('qrc:/console/'))
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
            if 'error' in msg or 'exceptionDetails' in msg['result']: raise RuntimeError(msg)
            return msg['result']['result'].get('value')

def press(selector):
    return js(f"(() => {{ const b = document.querySelector({json.dumps(selector)}); if (b && !b.disabled) {{ b.click(); return true }} return false }})()")

def pids(pattern):
    return subprocess.run(['pgrep', '-f', pattern], capture_output=True, text=True).stdout.split()

game_pattern, assistant_pattern = 'org[.]prismlauncher[.]EntryPoint', 'local/crash_assistant'

for _ in range(60):
    if js("document.querySelector('.bp-container')?.dataset.connected === 'true'"): break
    time.sleep(0.5)
else:
    sys.exit('UI never connected to Prism')
time.sleep(2)
if js("!!document.querySelector('[role=dialog] #dialog-title')?.textContent.includes('Welcome')"):
    js("[...document.querySelectorAll('button')].find(b => b.textContent.includes('Keep current settings'))?.click()"); time.sleep(1)
print('Selected CrazyCraft card:', js("(() => { const c = [...document.querySelectorAll('.instance-card')].find(c => c.textContent.includes('CrazyCraft')); if (c && !c.classList.contains('selected')) c.click(); return !!c })()"))
time.sleep(1)
print('Opened worlds:', press('[data-action=worlds]')); time.sleep(2)
print('Chose the world copy:', press(f'.world-row[data-world="{world}"]')); time.sleep(1)
pressed_at = time.time()
print('Pressed Play this world:', press('.world-play'))

# Into the world: the integrated server logs the player joining.
joined = False
while time.time() - pressed_at < 600:
    prompt = js("document.querySelector('.dialog-compact #dialog-title')?.textContent || null")
    if prompt == 'Offline player name':
        print('Offline prompt -> Continue:', js("(() => { const b = [...document.querySelectorAll('.dialog-compact button')].find(b => b.dataset.choice === 'Continue' || b.textContent.trim() === 'Continue'); b?.click(); return !!b })()"))
    elif prompt:
        failures.append(f'prompt: {prompt}'); break
    if js("!!document.querySelector('.launch-overlay.failed')"):
        failures.append('launch failed: ' + (js("document.querySelector('.launch-overlay')?.innerText.slice(0, 300)") or '')); break
    if log_path.exists() and log_path.stat().st_mtime > pressed_at and 'joined the game' in log_path.read_text(errors='replace'):
        joined = True; break
    time.sleep(2)
print(f'In the world: {joined} after {int(time.time() - pressed_at)}s')
if not joined and not failures: failures.append('never joined the world')

if joined:
    time.sleep(20)  # let the world settle (autosave timers start, chunks load)
    game, assistant = pids(game_pattern), pids(assistant_pattern)
    print('Game Java:', game, '| Crash Assistant:', assistant or 'not running (its part is not covered)')
    saved_before = level.stat().st_mtime
    print('Stop pressed:', press('[data-action=stop-game]')); time.sleep(1)
    stopped_at = time.time()
    print('Confirmed stop:', press('[data-action=confirm-stop]')); time.sleep(0.5)
    status = js("document.querySelector('.stop-status')?.textContent.trim() || null")
    print('Stop card says:', status, '| force close offered:', js("!!document.querySelector('[data-action=force-close]')"))
    if status != 'Saving and closing…': failures.append(f'stop card said {status!r}')
    gone_after = None
    while time.time() - stopped_at < 25:
        if not pids(game_pattern) and not pids(assistant_pattern): gone_after = time.time() - stopped_at; break
        time.sleep(0.5)
    print('Java and Crash Assistant gone after:', f'{gone_after:.1f}s' if gone_after is not None else 'NOT within 25 s',
          '| left:', pids(game_pattern), pids(assistant_pattern))
    if gone_after is None: failures.append('game or Crash Assistant still running 25 s after Stop')
    written = level.stat().st_mtime - stopped_at
    regions = [p for p in level.parent.rglob('*.mca') if p.stat().st_mtime >= stopped_at]
    print(f'level.dat written {written:+.1f}s after Stop (before: {saved_before - stopped_at:+.1f}s) | region files written after Stop: {len(regions)}')
    if written < 0: failures.append('level.dat not written after Stop')
    time.sleep(3)
    outcome = js("JSON.stringify({ failed: !!document.querySelector('.failed-step'), dialog: document.querySelector('[role=dialog] #dialog-title')?.textContent || null, stop: !!document.querySelector('[data-action=stop-game]') })")
    print('Launcher after the game closed:', outcome)
    if json.loads(outcome)['failed']: failures.append('launcher showed a failure or crash after a requested Stop')
ws.close()
print('STOP CHECK', 'FAILED: ' + '; '.join(failures) if failures else 'PASSED')
sys.exit(1 if failures else 0)
