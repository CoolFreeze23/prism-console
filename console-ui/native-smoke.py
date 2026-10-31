"""Run inside the isolated builder, against its loopback-only Qt DevTools.

Only use the disposable instance `console-smoke`; never a real data directory.
The launcher must already be running under Xvfb with remote debugging enabled.
"""
import base64
import os
import json
import time
import urllib.request
from pathlib import Path
import websocket

for attempt in range(30):
    try:
        targets = json.load(urllib.request.urlopen('http://127.0.0.1:9222/json'))
        target = next(t for t in targets if t.get('url', '').startswith('qrc:/console/'))
        break
    except Exception:
        time.sleep(1)
else:
    raise RuntimeError('Prism Console page did not become available')
ws = websocket.create_connection(target['webSocketDebuggerUrl'], timeout=15, suppress_origin=True)
serial = 0
def command(method, params=None):
    global serial
    serial += 1
    ws.send(json.dumps({'id': serial, 'method': method, 'params': params or {}}))
    while True:
        msg = json.loads(ws.recv())
        if msg.get('id') == serial:
            if 'error' in msg:
                raise RuntimeError(msg['error'])
            return msg['result']

def evaluate(js):
    result = command('Runtime.evaluate', {'expression': js, 'awaitPromise': True, 'returnByValue': True})
    if 'exceptionDetails' in result:
        raise RuntimeError(result['exceptionDetails'])
    return result['result'].get('value')

# Wait for the production UI to establish its connection first. The test then
# takes over the transport with its own channel; no UI actions are issued while
# that channel owns it. Reload afterward to restore the normal renderer.
for attempt in range(30):
    if evaluate("document.querySelector('.bp-container')?.dataset.connected === 'true'"): break
    time.sleep(.25)
else:
    raise RuntimeError('The production UI never connected')
evaluate('new Promise(resolve => new QWebChannel(qt.webChannelTransport, c => { window.nativeTest = c.objects.prism; resolve(true) }))')
def call(method, **args):
    return evaluate(f'new Promise(resolve => nativeTest.request({json.dumps(method)}, {json.dumps(args)}, resolve))')

snapshot = call('snapshot')
assert snapshot['ok'], snapshot
assert len(snapshot['data']['instances']) == 1, 'Refusing to test a non-isolated library'
assert snapshot['data']['instances'][0]['id'] == 'console-smoke', snapshot
assert snapshot['data']['instances'][0]['version'] == '1.21.1', snapshot
assert not snapshot['data']['accounts'], 'Refusing to test real accounts'
assert not call('completeSetup', automaticJava='yes')['ok']
assert call('completeSetup', automaticJava=False, keepExisting=True)['ok']
assert not call('snapshot')['data']['setup']['required']
assert not call('accountDetails', account='missing')['ok']
assert not call('removeAccount', account='missing', confirmName='')['ok']
assert not call('copyInstance', id='console-smoke', name='')['ok']
assert not call('launch', id='../not-an-instance')['ok']
assert not call('nativeDialog', route='arbitraryCommand')['ok']
assert not call('saveInstance', id='console-smoke', name='Test', minMemory=8192, maxMemory=2048)['ok']
assert not call('saveGlobalSettings', values={'ProxyPass': 'should-not-write'})['ok']
assert not call('saveGlobalSettings', values={'MinMemAlloc': 8192, 'MaxMemAlloc': 1024})['ok']
assert call('saveGlobalSettings', values={'MinMemAlloc': 512, 'MaxMemAlloc': 4096, 'AutomaticJavaSwitch': True})['ok']
defaults = call('globalSettings')['data']
assert defaults['MaxMemAlloc'] == 4096 and defaults['AutomaticJavaSwitch'] is True
assert call('saveInstancePreferences', id='console-smoke', values={'OverrideMemory': True, 'MinMemAlloc': 768, 'MaxMemAlloc': 6144})['ok']
assert call('instancePreferences', id='console-smoke')['data']['MaxMemAlloc'] == 6144
assert call('globalSettings')['data']['MaxMemAlloc'] == 4096, 'Instance write changed global defaults'
assert call('saveInstancePreferences', id='console-smoke', values={'OverrideMemory': False})['ok']
assert call('instancePreferences', id='console-smoke')['data']['MaxMemAlloc'] == 4096, 'Disabled override did not inherit'
assert not call('saveInstancePreferences', id='console-smoke', values={'NumberOfConcurrentDownloads': 2})['ok']
assert not call('browseFiles', path='/etc')['ok']
assert not call('installLocalResource', id='console-smoke', kind='mods', path='/does/not/exist.jar')['ok']
assert not call('importInstance', name='Test', source='http://example.com/pack.zip')['ok']
assert not call('importInstance', name='Test', source='/does/not/exist.mrpack')['ok']
components = call('components', id='console-smoke')
assert components['ok'] and any(c['id'] == 'net.minecraft' for c in components['data']), components
assert not call('setComponentVersion', id='console-smoke', component='arbitrary', version='1')['ok']
assert not call('confirmCatalogInstall', id='console-smoke', kind='mods', project='unselected')['ok']
assert call('cancelTask')['ok']
assert call('saveInstance', id='console-smoke', name='Controller test', minMemory=2048, maxMemory=4096)['ok']
updated = call('snapshot')['data']['instances'][0]
assert updated['name'] == 'Controller test' and updated['maxMemory'] == 4096, updated

for attempt in range(20):
    mods = call('resources', id='console-smoke', kind='mods')
    if mods.get('data'):
        break
    time.sleep(.5)
assert mods.get('data'), mods
resource = mods['data'][0]
assert call('setResourceEnabled', id='console-smoke', kind='mods', resourceId=resource['id'], enabled=False)['ok']
time.sleep(1)
resource = call('resources', id='console-smoke', kind='mods')['data'][0]
assert resource['enabled'] is False, resource
assert call('setResourceEnabled', id='console-smoke', kind='mods', resourceId=resource['id'], enabled=False)['ok']
time.sleep(1)
resource = call('resources', id='console-smoke', kind='mods')['data'][0]
assert resource['enabled'] is False, 'Repeated disable accidentally re-enabled the resource'
assert call('setResourceEnabled', id='console-smoke', kind='mods', resourceId=resource['id'], enabled=True)['ok']

assert evaluate("document.querySelector('.bp-container')?.dataset.connected === 'true'"), 'UI never connected to native bridge'
command('Page.reload')
time.sleep(2)
command('Emulation.setDeviceMetricsOverride', {'width': 1280, 'height': 800, 'deviceScaleFactor': 1, 'mobile': False})
time.sleep(1)
assert evaluate('Array.from(document.images).every(i => i.complete && i.naturalWidth > 0)'), 'Broken image in the native UI'
png = command('Page.captureScreenshot', {'format': 'png'})['data']
Path(os.environ.get('PRISM_SMOKE_IMAGE', '/build/native-smoke.png')).write_bytes(base64.b64decode(png))
print('PASS: real Prism library, validated settings mutation, invalid ID/route rejection, real mod disable/re-enable, idempotent disable, QWebChannel UI connection. No account login or game launch performed.')
ws.close()
