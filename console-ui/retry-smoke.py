"""A network that drops a connection, on the disposable console-smoke library only.

`prepare` (Prism stopped): sends Prism's traffic through a local proxy and shortens its request timeout to 5 s.
`proxy`: the proxy. The first connection to Prism's metadata server is accepted, never answered, and dropped
after 7 s (past Prism's timeout), as when the network drops new connections in bursts; every other connection
goes through. `run` (Prism started):
opens New instance as a player does and checks that the Minecraft version list still loads, with no error on
screen, after Prism quietly retried the stalled request. `restore` (Prism stopped): puts the settings back.
"""
import json
import os
import socket
import sys
import threading
import time
import urllib.request
from pathlib import Path

library = Path(os.environ.get('PRISM_SMOKE_LIBRARY', '/build/console-smoke-data'))
cfg, saved = library / 'prismlauncher.cfg', library / 'prismlauncher.cfg.retry'
PORT, META = 18899, 'meta.prismlauncher.org'
TEST_KEYS = {'ProxyType': 'HTTP', 'ProxyAddr': '127.0.0.1', 'ProxyPort': str(PORT), 'RequestTimeout': '5'}
proxy_log = Path('/build/retry-proxy.log')

if sys.argv[1:] == ['prepare']:
    text = cfg.read_text() if cfg.exists() else ''
    saved.write_text(text)
    # Qt INI: the keys live under [General]; replace them there, or add them right after the header.
    lines = [line for line in text.splitlines() if line.split('=', 1)[0] not in TEST_KEYS]
    at = lines.index('[General]') + 1 if '[General]' in lines else 0
    if not at: lines.insert(0, '[General]'); at = 1
    lines[at:at] = [f'{k}={v}' for k, v in TEST_KEYS.items()]
    cfg.write_text('\n'.join(lines) + '\n')
    sys.exit(0)
if sys.argv[1:] == ['restore']:
    if saved.exists():
        cfg.write_text(saved.read_text()); saved.unlink()
    sys.exit(0)

if sys.argv[1:] == ['proxy']:
    stalled = threading.Event()
    def note(line):
        with proxy_log.open('a') as f: f.write(f'{time.time():.1f} {line}\n')
    def pipe(src, dst):
        try:
            while data := src.recv(65536): dst.sendall(data)
        except OSError: pass
        finally:
            for s in (src, dst):
                try: s.shutdown(socket.SHUT_RDWR)
                except OSError: pass
    def handle(client):
        head = b''
        while b'\r\n\r\n' not in head:
            chunk = client.recv(4096)
            if not chunk: client.close(); return
            head += chunk
        method, target = head.split(b' ', 2)[:2]
        if method != b'CONNECT':
            note(f'refused {method.decode()} {target.decode()}')
            client.sendall(b'HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n'); client.close(); return
        host, port = target.decode().rsplit(':', 1)
        if host == META and not stalled.is_set():
            stalled.set(); note(f'stalled {host}')
            client.settimeout(7)
            try:
                while client.recv(4096): pass  # never answer: Prism times out first, then the connection is dropped
            except OSError: pass
            note(f'dropped {host}'); client.close(); return
        try:
            upstream = socket.create_connection((host, int(port)), timeout=20)
        except OSError as e:
            note(f'unreachable {host} {e}'); client.close(); return
        note(f'passed {host}')
        client.sendall(b'HTTP/1.1 200 Connection established\r\n\r\n')
        threading.Thread(target=pipe, args=(upstream, client), daemon=True).start()
        pipe(client, upstream)
    server = socket.create_server(('127.0.0.1', PORT))
    note('listening')
    while True:
        conn, _ = server.accept()
        threading.Thread(target=handle, args=(conn,), daemon=True).start()

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
def wait(js, what, seconds=60):
    for _ in range(seconds * 4):
        value = evaluate(js)
        if value: return value
        time.sleep(.25)
    print('Page shows:', evaluate("document.body.innerText.slice(0, 600)"))
    raise AssertionError(f'Timed out: {what}')

wait("document.querySelector('.bp-container')?.dataset.connected === 'true'", 'page connected')
wait("(() => { const b = document.querySelector('[data-action=add-instance]'); if (!b || b.disabled) return false; b.click(); return true })()", 'New instance')
count = wait("document.querySelectorAll('.version-list button').length", 'Minecraft version list', 90)
errors = evaluate("[...document.querySelectorAll('.error, .workflow-error, .launch-overlay.failed')].map(e => e.textContent.trim()).filter(Boolean)")
assert not errors, f'error on screen: {errors}'
proxy = proxy_log.read_text() if proxy_log.exists() else ''
assert f'stalled {META}' in proxy, 'the proxy never saw a metadata connection to stall'
assert f'passed {META}' in proxy, 'no metadata connection went through after the stall'
logs = '\n'.join(p.read_text(errors='replace') for p in [library / 'logs' / 'PrismLauncher-0.log', Path('/build/native-launcher-retry.log')] if p.exists())
retried = [line.strip() for line in logs.splitlines() if 'Retrying' in line and META in line]
assert retried, 'no retry of the metadata request in the log'
print(f"PASS: first connection to {META} stalled, Prism retried quietly ({retried[0].split('Retrying', 1)[1].strip()[:110]}); "
      f"New instance lists {count} Minecraft versions, no error shown")
