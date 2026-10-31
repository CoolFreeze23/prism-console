"""A launch with Minecraft's servers unreachable, on the disposable console-smoke library only.

`prepare` (Prism stopped): adds a placeholder Microsoft account with no real tokens and sends all of Prism's
traffic to a closed local proxy port, so sign-in cannot connect, as on dead hotel Wi-Fi. `run` (Prism started):
plays the instance through the console and checks that the player gets the console's own error view with a
plain reason in English and Portuguese, Try again and Show log. `restore` (Prism stopped): puts the library back.
"""
import json
import os
import sys
import time
import urllib.request
from pathlib import Path

library = Path(os.environ.get('PRISM_SMOKE_LIBRARY', '/build/console-smoke-data'))
cfg, accounts, saved = library / 'prismlauncher.cfg', library / 'accounts.json', library / 'prismlauncher.cfg.launch-failure'
CLIENT = '00000000-0000-0000-0000-00000000c0de'
# Starts in Portuguese: switching to it later would download Prism's translation, which the dead proxy blocks.
TEST_KEYS = {'ProxyType': 'HTTP', 'ProxyAddr': '127.0.0.1', 'ProxyPort': '9', 'MSAClientIDOverride': CLIENT, 'Language': 'pt_BR'}

if sys.argv[1:] == ['prepare']:
    if accounts.exists():
        assert not json.loads(accounts.read_text() or '{}').get('accounts'), 'Refusing to replace real accounts'
        accounts.rename(library / 'accounts.json.launch-failure')
    text = cfg.read_text() if cfg.exists() else ''
    saved.write_text(text)
    # Qt INI: the keys live under [General]; replace them there, or add them right after the header.
    lines = [line for line in text.splitlines() if line.split('=', 1)[0] not in TEST_KEYS]
    at = lines.index('[General]') + 1 if '[General]' in lines else 0
    if not at: lines.insert(0, '[General]'); at = 1
    lines[at:at] = [f'{k}={v}' for k, v in TEST_KEYS.items()]
    cfg.write_text('\n'.join(lines) + '\n')
    accounts.write_text(json.dumps({'formatVersion': 3, 'accounts': [{
        'type': 'MSA', 'msa-client-id': CLIENT, 'active': True,
        'msa': {'token': 'smoke-placeholder', 'refresh_token': 'smoke-placeholder', 'iat': 0, 'exp': 1},
        'profile': {'id': '000000000000000000000000000c0de5', 'name': 'SmokePlayer', 'skin': {'id': '', 'url': '', 'variant': ''}, 'capes': []},
        'entitlement': {'ownsMinecraft': True, 'canPlayMinecraft': True}}]}))
    sys.exit(0)
if sys.argv[1:] == ['restore']:
    accounts.unlink(missing_ok=True)
    if (library / 'accounts.json.launch-failure').exists():
        (library / 'accounts.json.launch-failure').rename(accounts)
    if saved.exists():
        cfg.write_text(saved.read_text()); saved.unlink()
    sys.exit(0)

import websocket
for _ in range(90):
    try:
        target = next(t for t in json.load(urllib.request.urlopen('http://127.0.0.1:9222/json')) if t.get('url', '').startswith('qrc:/console/'))
        break
    except Exception:
        time.sleep(1)
else:
    sys.exit('Prism did not come back up')
time.sleep(3)
ws = websocket.create_connection(target['webSocketDebuggerUrl'], timeout=30, suppress_origin=True)
serial = 0
def evaluate(js):
    global serial
    serial += 1
    ws.send(json.dumps({'id': serial, 'method': 'Runtime.evaluate', 'params': {'expression': js, 'awaitPromise': True, 'returnByValue': True}}))
    while True:
        msg = json.loads(ws.recv())
        if msg.get('id') == serial:
            assert 'exceptionDetails' not in msg['result'], msg['result']
            return msg['result']['result'].get('value')
# Everything below is what a player does on screen. No second QWebChannel: one on the page's transport would
# take the page's own replies, and the page would stop updating.
def click(selector):
    return evaluate(f"(() => {{ const b = document.querySelector({json.dumps(selector)}); if (!b || b.disabled) return false; b.click(); return true }})()")
def wait(js, what, seconds=60):
    for _ in range(seconds * 4):
        value = evaluate(js)
        if value: return value
        time.sleep(.25)
    print('Page shows:', evaluate("document.body.innerText.slice(0, 600)"))
    raise AssertionError(f'Timed out: {what}')

wait("document.querySelector('.bp-container')?.dataset.connected === 'true'", 'page connected')
account = wait("document.querySelector('.account-chip > span:last-child')?.textContent.trim()", 'account chip')
assert account == 'SmokePlayer', f'Refusing to launch with a real account ({account})'
view = """(() => { const r = document.querySelector('.launch-overlay [role=alert][data-reason]'); if (!r) return null; const o = r.closest('.launch-overlay');
  return { reason: r.dataset.reason, text: r.textContent.trim(), eyebrow: o.querySelector('.eyebrow')?.textContent.trim(), actions: [...o.querySelectorAll('[data-action]')].map(b => b.dataset.action) } })()"""

def play_until_failure():
    assert click('[data-action=play]'), 'Play is not available'
    prompts, pressed = [], 0
    for _ in range(240):
        failed = evaluate(view)
        if failed: return failed, prompts
        title = evaluate("document.querySelector('.dialog-compact #dialog-title')?.textContent.trim() || ''")
        if title and time.time() - pressed > 2:
            # Sign-in could not connect: Prism offers offline play, and a player continues (pressing again
            # if the prompt is still there a moment later).
            prompts.append(title); pressed = time.time()
            assert click('.dialog-compact .actions button:last-child'), title
        assert not evaluate("!!document.querySelector('.running-banner')"), 'the game started although the servers were unreachable'
        time.sleep(.25)
    print('Page shows:', evaluate("document.body.innerText.slice(0, 600)"))
    raise AssertionError('no console error view after Play')

portuguese, prompts = play_until_failure()
assert portuguese['reason'] in ('network', 'offlineFiles'), portuguese
assert portuguese['eyebrow'] == 'Não foi possível iniciar o jogo', portuguese
assert {'launch-retry', 'launch-log'} <= set(portuguese['actions']), portuguese
assert click('[data-action=launch-close]')

# The same in English, switched the way a player does it: Settings, Language.
assert click('.bp-nav .bp-nav-btn:last-of-type')
wait("!!document.querySelector('[data-action=language-en]')", 'language choice')
click('[data-action=language-en]')
wait("document.querySelector('[data-action=language-en]')?.getAttribute('aria-checked') === 'true'", 'English chosen')
click('.bp-nav .bp-nav-btn')
wait("!!document.querySelector('[data-action=play]:not(:disabled)')", 'Play ready again')
english, _ = play_until_failure()
assert english['reason'] == portuguese['reason'] and english['text'] != portuguese['text'], english
assert {'launch-retry', 'launch-log'} <= set(english['actions']), english
assert 'Please' not in english['text'] and 'Proxy' not in english['text'], english
click('[data-action=launch-close]')
print(f"PASS: servers unreachable -> prompts {prompts} -> console error view, reason '{english['reason']}': "
      f"PT {portuguese['text']!r} / EN {english['text']!r}; Try again and Show log offered")
