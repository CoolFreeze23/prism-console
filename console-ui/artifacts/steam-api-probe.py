# Read-only probe of the Steam client's JS API on the handheld (CEF port 8080). Changes nothing.
import json, sys, urllib.request
import websocket

targets = json.load(urllib.request.urlopen('http://127.0.0.1:8080/json'))
print('Targets:', sorted({t.get('title', '') for t in targets})[:12])
shared = next(t for t in targets if t.get('title') == 'SharedJSContext')
ws = websocket.create_connection(shared['webSocketDebuggerUrl'], timeout=30, suppress_origin=True)
ws.send(json.dumps({'id': 1, 'method': 'Runtime.evaluate', 'params': {'returnByValue': True, 'awaitPromise': True, 'expression': '''(() => {
  const need = ['AddShortcut', 'SetShortcutName', 'SetShortcutExe', 'SetShortcutStartDir', 'SetShortcutLaunchOptions', 'SetShortcutIcon', 'SetCustomArtworkForApp', 'RemoveShortcut'];
  const apps = window.SteamClient?.Apps || {};
  return {
    missing: need.filter(n => typeof apps[n] !== 'function'),
    appStore: typeof window.appStore?.GetAppOverviewByAppID,
    shortcuts: Object.keys(apps).filter(k => /Shortcut|Artwork/.test(k)).sort(),
  };
})()'''}}))
while True:
    reply = json.loads(ws.recv())
    if reply.get('id') == 1:
        print(json.dumps(reply.get('result', {}).get('result', {}).get('value', reply), indent=1))
        break
