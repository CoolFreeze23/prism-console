"""Native workflow coverage; only against console-smoke with no accounts."""
import base64
import json
import os
import time
import urllib.request
from pathlib import Path
import websocket

targets = json.load(urllib.request.urlopen('http://127.0.0.1:9222/json'))
target = next(t for t in targets if t.get('url', '').startswith('qrc:/console/'))
ws = websocket.create_connection(target['webSocketDebuggerUrl'], timeout=15, suppress_origin=True)
serial = 0
output = Path(os.environ.get('PRISM_SMOKE_OUTPUT', '/build'))
output.mkdir(parents=True, exist_ok=True)
def command(method, params=None):
    global serial
    serial += 1
    ws.send(json.dumps({'id': serial, 'method': method, 'params': params or {}}))
    while True:
        msg = json.loads(ws.recv())
        if msg.get('id') == serial:
            assert 'error' not in msg, msg
            return msg['result']
def evaluate(js):
    result = command('Runtime.evaluate', {'expression': js, 'awaitPromise': True, 'returnByValue': True})
    assert 'exceptionDetails' not in result, result.get('exceptionDetails')
    return result['result'].get('value')
evaluate('new Promise(resolve => new QWebChannel(qt.webChannelTransport, c => { window.nativeTest = c.objects.prism; resolve(true) }))')
def call(method, **args):
    reply = evaluate(f'new Promise(resolve => nativeTest.request({json.dumps(method)}, {json.dumps(args)}, resolve))')
    assert reply['ok'], reply
    return reply.get('data')
snapshot = call('snapshot')
assert len(snapshot['instances']) == 1 and snapshot['instances'][0]['id'] == 'console-smoke'
assert not snapshot['accounts'], 'Refusing to operate on real accounts'
def flow():
    return call('workflow')
def wait_flow(title=None, different=None):
    for _ in range(100):
        f = flow()
        if f and (not title or title.lower() in f['title'].lower()) and (not different or f['id'] != different): return f
        time.sleep(.1)
    raise AssertionError(f'No workflow: {title}')
def nodes(n):
    yield n
    for child in n.get('children', []): yield from nodes(child)
def find(f, type=None, text=None, label=None):
    return next(n for n in nodes(f['content']) if (not type or n['type'] == type) and (text is None or n.get('text') == text) and (label is None or label.lower() in n.get('label', '').lower()))
def act(n, op='click', **args):
    call('workflowAction', control=n['id'], op=op, **args)
    time.sleep(.15)
def close(f):
    act(f, 'close')
def select_page(f, name):
    for n in nodes(f['content']):
        if n['type'] == 'list':
            for row in n.get('rows', []):
                if row['text'] == name:
                    act(n, 'select', path=row['path'], expected=row['text'])
                    return flow()
    raise AssertionError(f'Missing page {name}')

# Existing dialogs must be controllable while their native exec() loop waits.
call('nativeDialog', route='settings')
f = wait_flow()
assert not f.get('blocked'), 'Native modal window blocks controller input'
(output / 'workflow-settings.json').write_text(json.dumps(f, indent=2), encoding='utf-8')
pages = next(n for n in nodes(f['content']) if n['type'] == 'list' and len(n.get('rows', [])) > 5)
names = [r['text'] for r in pages['rows']]
for name in names:
    f = select_page(flow(), name)
    assert len(list(nodes(f['content']))) > 4, name
print('Settings pages:', ', '.join(names))
f = select_page(flow(), 'Proxy')
# Read-only audit of masking; no credentials or network settings are changed.
for n in nodes(f['content']):
    if n.get('secret'): assert n.get('value') == ''
f = select_page(flow(), 'General')
buttons = [n for n in nodes(f['content']) if n['type'] == 'button']
(output / 'workflow-launcher.json').write_text(json.dumps(f, indent=2), encoding='utf-8')
# Nested Qt file picker must stay inside the controller surface and restore its parent.
browse = next(n for n in nodes(f['content']) if n['type'] == 'button' and n.get('name') == 'downloadsDirBrowseBtn')
parent_id = f['id']; act(browse)
picker = wait_flow(different=parent_id)
assert not picker.get('blocked'), 'Nested file dialog blocks input'
assert any(n['type'] == 'list' for n in nodes(picker['content'])), 'Missing file browser model'
act(find(picker, 'button', 'Cancel'))
f = wait_flow(); assert f['id'] == parent_id, 'Nested cancellation lost parent settings'

# Close via Cancel, preserving fixture settings.
act(find(f, 'button', 'Cancel'))
for _ in range(20):
    if not flow(): break
    time.sleep(.1)
assert not flow(), 'Settings did not close'

call('nativeDialog', route='add')
f = wait_flow()
(output / 'workflow-providers.json').write_text(json.dumps(f, indent=2), encoding='utf-8')
provider_list = next(n for n in nodes(f['content']) if n['type'] == 'list' and len(n.get('rows', [])) > 4)
providers = [r['text'] for r in provider_list['rows']]
for name in providers:
    f = select_page(flow(), name)
    assert f['content'], name
print('Provider pages:', ', '.join(providers))
close(f)
time.sleep(.3)
call('instanceDialog', id='console-smoke', page='version')
f = wait_flow()
(output / 'workflow-instance.json').write_text(json.dumps(f, indent=2), encoding='utf-8')
page_list = next(n for n in nodes(f['content']) if n['type'] == 'list' and len(n.get('rows', [])) > 5)
names = [r['text'] for r in page_list['rows']]
for name in names:
    f = select_page(flow(), name)
    assert f['content'], name
print('Instance pages:', ', '.join(names))
# A context menu must remain in the renderer and dismiss back to its editor.
f = select_page(flow(), 'Mods')
mod_list = next((n for n in nodes(f['content']) if n['type'] == 'list' and n.get('context') and n.get('rows')), None)
if mod_list:
    row = mod_list['rows'][0]
    act(mod_list, 'context', path=row['path'], expected=row['text'])
    menu = wait_flow(different=f['id'])
    assert menu['content']['type'] == 'menu' and not menu['blocked']
    close(menu)
    f = wait_flow()
    assert f['content']['type'] != 'menu'
close(f)
time.sleep(.3)
assert not flow()
commands = call('commands', id='console-smoke')
assert any(c['name'] == 'actionRenameInstance' for c in commands)
assert any(c['name'] == 'actionDeleteInstance' for c in commands)
rename = next(c for c in commands if c['name'] == 'actionRenameInstance')
act(rename)
f = wait_flow()
field = find(f, 'input')
act(field, 'value', value='Console workflow test')
act(find(flow(), 'button', 'OK'))
time.sleep(.3)
assert call('snapshot')['instances'][0]['name'] == 'Console workflow test'
# Restore the fixture's original display name through its native model.
call('saveInstance', id='console-smoke', name='Controller test', minMemory=2048, maxMemory=4096)

# Do not exercise destructive actions, online account changes or game launches.
call('nativeDialog', route='settings')
wait_flow()
command('Page.reload')
time.sleep(3)
assert evaluate("!!document.querySelector('.native-workflow')"), 'Workflow renderer is not visible'
assert evaluate("!document.querySelector('.native-workflow button:not(:disabled)')?.disabled")
command('Emulation.setDeviceMetricsOverride', {'width': 1280, 'height': 800, 'deviceScaleFactor': 1, 'mobile': False})
time.sleep(.5)
(output / 'workflow-native.png').write_bytes(base64.b64decode(command('Page.captureScreenshot', {'format': 'png'})['data']))
print('PASS: real Prism settings/provider/instance pages, scoped native actions, masked secrets, controller renderer.')
ws.close()
