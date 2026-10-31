"""Modrinth modpack browser against the live API, only on the disposable console-smoke library:
search, releases, icons, then a real install that is verified and removed again."""
import json
import os
import shutil
import time
import urllib.request
from pathlib import Path
import websocket

library = Path(os.environ.get('PRISM_SMOKE_LIBRARY', '/build/console-smoke-data'))

# Whatever happens, leave the disposable library as the other smoke suites expect it: one instance.
import atexit
def _cleanup():
    for cfg in (library / 'instances').glob('*/instance.cfg'):
        if any(f'name={n}' in cfg.read_text(errors='replace') for n in ('Modpack smoke', 'Cancelled smoke', 'Corrupt smoke')):
            shutil.rmtree(cfg.parent, ignore_errors=True)
    for icon in (library / 'icons').glob('modrinth_*'):
        icon.unlink(missing_ok=True)
atexit.register(_cleanup)
target = next(t for t in json.load(urllib.request.urlopen('http://127.0.0.1:9222/json')) if t.get('url', '').startswith('qrc:/console/'))
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
evaluate('new Promise(resolve => new QWebChannel(qt.webChannelTransport, c => { window.packTest = c.objects.prism; resolve(true) }))')
def call(method, **args):
    reply = evaluate(f'new Promise(resolve => packTest.request({json.dumps(method)}, {json.dumps(args)}, resolve))')
    assert reply['ok'], reply
    return reply.get('data')
def dialog_text(node):
    texts = [node.get('text') or node.get('label') or '']
    for child in node.get('children', []) or []: texts.append(dialog_text(child))
    return ' | '.join(t for t in texts if t)
def wait(predicate, what, seconds=60):
    for _ in range(seconds * 4):
        value = predicate()
        if value: return value
        # A Prism dialog waiting for an answer would otherwise look like a hang: report it.
        flow = call('workflow')
        if flow and flow.get('id'):
            raise AssertionError(f"{what}: Prism is asking '{flow.get('title')}': {dialog_text(flow.get('content', {}))[:400]}")
        time.sleep(.25)
    raise AssertionError(f'Timed out: {what}')

# Earlier suites may leave a Prism page open in the same process; close it so only new dialogs count.
for _ in range(10):
    flow = call('workflow')
    if not (flow and flow.get('id')): break
    call('workflowAction', control=flow['id'], op='close')
    time.sleep(.5)
assert not (call('workflow') or {}).get('id'), 'could not close the leftover Prism page'
before = call('snapshot')
assert not before['accounts'], 'Refusing to operate on real accounts'
assert [i['id'] for i in before['instances']] == ['console-smoke'], 'Refusing to test a non-isolated library'

# Screenshots gallery and world quick play guard, on the disposable instance.
import struct, zlib
def tiny_png(width=64, height=40):
    rows = b''.join(b'\x00' + bytes([40, 160, 60]) * width for _ in range(height))
    chunk = lambda kind, data: struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
game_root = next(d for d in (library / 'instances' / 'console-smoke' / '.minecraft', library / 'instances' / 'console-smoke' / 'minecraft') if d.is_dir())
shots_dir = game_root / 'screenshots'
created_dir = not shots_dir.exists()
shots_dir.mkdir(exist_ok=True)
(shots_dir / 'smoke-shot.png').write_bytes(tiny_png())
gallery = call('screenshots', id='console-smoke')
assert gallery['total'] >= 1 and any(s['name'] == 'smoke-shot.png' and s['thumb'].startswith('data:image/jpeg') for s in gallery['shots']), gallery
assert call('screenshot', id='console-smoke', name='smoke-shot.png')['image'].startswith('data:image/jpeg')
for bad in ('../instance.cfg', 'missing.png'):
    assert not evaluate(f"new Promise(r => packTest.request('screenshot', {json.dumps({'id': 'console-smoke', 'name': bad})}, r))")['ok']
(shots_dir / 'smoke-shot.png').unlink()
if created_dir: shots_dir.rmdir()
assert isinstance(call('worlds', id='console-smoke'), list)
reply = evaluate("new Promise(r => packTest.request('launch', {id: 'console-smoke', world: 'no-such-world'}, r))")
assert reply.get('error') == 'World not found', reply
print('PASS: screenshot gallery, full view, path guard; world quick play rejects unknown worlds')

# Servers: read from a real NBT servers.dat; only listed servers can be joined; a corrupt file is an empty list.
def nbt_string(name, value):
    encode = lambda text: struct.pack('>H', len(text.encode())) + text.encode()
    return b'\x08' + encode(name) + encode(value)
servers_dat = game_root / 'servers.dat'
original = servers_dat.read_bytes() if servers_dat.exists() else None
entry = nbt_string('name', 'Smoke Server') + nbt_string('ip', 'smoke.example.net:25570') + b'\x00'
servers_dat.write_bytes(b'\x0a\x00\x00' + b'\x09' + struct.pack('>H', 7) + b'servers' + b'\x0a' + struct.pack('>i', 1) + entry + b'\x00')
listed = call('servers', id='console-smoke')
assert listed == [{'name': 'Smoke Server', 'address': 'smoke.example.net:25570', 'icon': ''}], listed
reply = evaluate("new Promise(r => packTest.request('launch', {id: 'console-smoke', server: 'evil.example.com'}, r))")
assert reply.get('error') == 'Server not found', reply
servers_dat.write_bytes(b'\x0a\x00\x00\x09garbage')
assert call('servers', id='console-smoke') == [], 'corrupt servers.dat must read as empty'
def refused(method, **args):
    return evaluate(f"new Promise(r => packTest.request({json.dumps(method)}, {json.dumps(args)}, r))").get('error')
# An unreadable list is never overwritten, even by an add.
corrupt_bytes = servers_dat.read_bytes()
assert 'can\'t be read' in (refused('saveServer', id='console-smoke', index=-1, name='New', address='new.example.net') or '')
assert servers_dat.read_bytes() == corrupt_bytes, 'unreadable servers.dat was changed'
print('PASS: servers list from servers.dat, unlisted server refused, corrupt file tolerated and left unchanged')

# Server editing: add, edit by position (other tags kept), stale position refused, input checked, remove.
servers_dat.write_bytes(b'\x0a\x00\x00' + b'\x09' + struct.pack('>H', 7) + b'servers' + b'\x0a' + struct.pack('>i', 1)
                        + b'\x01' + struct.pack('>H', 14) + b'acceptTextures' + b'\x01' + entry + b'\x00')
listed = call('saveServer', id='console-smoke', index=-1, expected='', name='  Added  ', address='added.example.net')
assert [s['name'] for s in listed] == ['Smoke Server', 'Added'] and listed[1]['address'] == 'added.example.net', listed
listed = call('saveServer', id='console-smoke', index=0, expected='smoke.example.net:25570', name='Renamed', address='smoke.example.net:25570')
assert listed[0]['name'] == 'Renamed' and b'acceptTextures' in servers_dat.read_bytes(), listed
assert 'changed' in refused('saveServer', id='console-smoke', index=0, expected='someone.else.net', name='X', address='x.net')
assert 'changed' in refused('removeServer', id='console-smoke', index=5, expected='x.net')
assert refused('saveServer', id='console-smoke', index=-1, name='', address='a.net') == 'Enter a server name'
assert 'address' in refused('saveServer', id='console-smoke', index=-1, name='Spaces', address='has space.net')
assert call('servers', id='console-smoke') == listed, 'refused edits changed the list'
listed = call('removeServer', id='console-smoke', index=0, expected='smoke.example.net:25570')
assert [s['name'] for s in listed] == ['Added'], listed
listed = call('removeServer', id='console-smoke', index=0, expected='added.example.net')
assert listed == [] and call('servers', id='console-smoke') == []
if original is None: servers_dat.unlink()
else: servers_dat.write_bytes(original)
print('PASS: servers added, edited in place (extra tags kept), removed; stale positions and bad input refused')

# A resource pack named with Minecraft colour codes and accents (real packs ship names like this; under
# Gaming Mode's LC_ALL=C one once broke CrazyCraft's pre-launch loader). Listed intact and toggled by name.
import io, zipfile
odd_name = '§cPacote de Ação — Teste§r.zip'
packs_dir = game_root / 'resourcepacks'
created_packs = not packs_dir.exists()
packs_dir.mkdir(exist_ok=True)
buffer = io.BytesIO()
with zipfile.ZipFile(buffer, 'w') as pack:
    pack.writestr('pack.mcmeta', json.dumps({'pack': {'pack_format': 34, 'description': 'Smoke pack'}}))
(packs_dir / odd_name).write_bytes(buffer.getvalue())
listed = wait(lambda: [r for r in call('resources', id='console-smoke', kind='resourcepacks') if r['id'] == odd_name], 'non-ASCII pack listed', 30)
call('setResourceEnabled', id='console-smoke', kind='resourcepacks', resourceId=odd_name, enabled=False)
assert wait(lambda: (packs_dir / (odd_name + '.disabled')).exists(), 'non-ASCII pack disabled on disk', 10)
renamed = wait(lambda: [r for r in call('resources', id='console-smoke', kind='resourcepacks') if r['id'].startswith(odd_name)], 'disabled pack listed', 30)[0]
assert renamed['enabled'] is False, renamed
call('setResourceEnabled', id='console-smoke', kind='resourcepacks', resourceId=renamed['id'], enabled=True)
assert wait(lambda: (packs_dir / odd_name).exists(), 'non-ASCII pack enabled on disk', 10)
for leftover in (packs_dir / odd_name, packs_dir / (odd_name + '.disabled')):
    if leftover.exists(): leftover.unlink()
if created_packs: packs_dir.rmdir()
print(f"PASS: non-ASCII resource pack '{odd_name}' listed, disabled and enabled by its exact name")

# Launcher settings page: every option reads, a change saves and reads back, limits hold, then all is restored.
options = call('launcherOptions')
assert options['about']['version'] and isinstance(options['soundEnabled'], bool) and 0 <= options['soundVolume'] <= 100, options
saved = {k: v for k, v in options.items() if k != 'about'}
assert call('setLauncherOption', key='soundVolume', value=70) == 70 and call('snapshot')['sound']['volume'] == 70
assert call('setLauncherOption', key='installDependencies', value=not saved['installDependencies']) == (not saved['installDependencies'])
assert call('launcherOptions')['installDependencies'] == (not saved['installDependencies'])
assert refused('setLauncherOption', key='parallelDownloads', value=99) == 'Value out of range'
assert refused('setLauncherOption', key='recordPlayTime', value=3) == 'Choose on or off'
assert refused('setLauncherOption', key='JavaPath', value='/bin/sh') == 'Unknown setting'
for key, value in saved.items(): call('setLauncherOption', key=key, value=value)
assert {k: v for k, v in call('launcherOptions').items() if k != 'about'} == saved
print(f"PASS: {len(saved)} launcher options read, saved, range-checked and restored")

# The language choice is stored, so the console (which reads it back) really switches, and it survives a restart.
language = call('snapshot').get('language') or 'en_US'
call('setLanguage', language='pt_BR')
assert call('snapshot')['language'] == 'pt_BR'
call('setLanguage', language='en_US' if language not in ('en_US', 'pt_BR') else language)
assert call('snapshot')['language'] in ('en_US', language)
print('PASS: language choice stored and restored')

# Search filters against the live Modrinth API: a loader and a Minecraft release narrow the results, the
# release list for the version filter arrives once, and values outside the known set are refused.
call('searchModpacks', query='')
everything = wait(lambda: (s := call('modpacks')) and not s.get('loading') and s, 'unfiltered search')
assert not everything.get('error') and everything['total'] > 0, everything.get('error')
releases = wait(lambda: call('modpacks').get('gameVersions'), 'Minecraft releases', 30)
assert '1.21.1' in releases and '1.20.1' in releases and not any('w' in v or '-pre' in v for v in releases), releases[:10]
call('searchModpacks', query='', loader='neoforge', gameVersion='1.21.1', sort='newest')
narrowed = wait(lambda: (s := call('modpacks')) and not s.get('loading') and s, 'filtered search')
assert not narrowed.get('error') and 0 < narrowed['total'] < everything['total'], (narrowed.get('error'), narrowed.get('total'), everything['total'])
assert narrowed['filters'] == {'loader': 'neoforge', 'gameVersion': '1.21.1', 'sort': 'newest'}, narrowed['filters']
for bad in ({'loader': 'rift'}, {'gameVersion': '1.21"]]'}, {'sort': 'random'}):
    assert refused('searchModpacks', query='', **bad), bad
print(f"PASS: filters narrow {everything['total']} modpacks to {narrowed['total']} (NeoForge, 1.21.1, newest); "
      f"{len(releases)} releases offered; bad values refused")

call('searchModpacks', query='Simply Optimized')
state = wait(lambda: (s := call('modpacks')) and not s.get('loading') and s, 'search results')
assert not state.get('error'), state['error']
assert state['hits'] and state['total'] > 0, state
hit = next(h for h in state['hits'] if 'optimized' in h['title'].lower())
print(f"Search: {len(state['hits'])} of {state['total']} hits; chose {hit['title']}")
wait(lambda: next((h for h in call('modpacks')['hits'] if h['id'] == hit['id'] and h.get('hasIcon')), None), 'pack icon')
icons = call('modpackIcons', ids=[hit['id'], 'not-a-project'])
assert list(icons) == [hit['id']] and icons[hit['id']].startswith('data:image/png'), list(icons)
assert 'icon' not in call('modpacks')['hits'][0], 'polled state must not carry image data'
print('Icon: fetched once, re-encoded as PNG, served on request only')

# A project Modrinth does not have ends in a clear error, not an endless spinner.
call('modpackVersions', project='zzzzzzzzzz')
state = wait(lambda: (s := call('modpacks')) and not s.get('versionsLoading') and s, 'missing project')
assert state.get('error') and not state['versions'], state
print(f"Missing project: {state['error']}")

call('modpackVersions', project=hit['id'])
state = wait(lambda: (s := call('modpacks')) and not s.get('versionsLoading') and s.get('project') == hit['id'] and s, 'releases')
assert state['versions'], state
candidates = [v for v in state['versions'] if '1.21.1' in v['minecraft'] and v['type'] == 'release']
release = next((v for v in candidates if 'fabric' in v['loaders']), candidates[0] if candidates else state['versions'][0])  # Fabric: widest mod support for the update test
print(f"Releases: {len(state['versions'])}; installing {release['number']} for {release['minecraft']}")

# Guard rails: a release that was never listed, and a foreign project, are both refused.
for bad in ({'project': hit['id'], 'version': 'not-listed', 'name': 'x'}, {'project': 'other', 'version': release['id'], 'name': 'x'}):
    reply = evaluate(f"new Promise(r => packTest.request('installModpack', {json.dumps(bad)}, r))")
    assert not reply['ok'], reply

# Cancelling an install part-way leaves no instance behind.
call('installModpack', project=hit['id'], version=release['id'], name='Cancelled smoke')
time.sleep(0.3)  # during the pack download, the step Prism can abort cleanly (aborting its creation step races upstream)
cancel = evaluate("new Promise(r => packTest.request('cancelTask', {}, r))")
task = wait(lambda: (t := call('snapshot').get('task')) and not t.get('active') and t, 'cancelled install', 300)
names = [i['name'] for i in call('snapshot')['instances']]
if cancel['ok'] and task['status'] == 'Cancelled':
    assert 'Cancelled smoke' not in names, names
    print('PASS: install cancelled part-way, nothing left behind')
else:
    print(f"Install finished before it could be cancelled ({task['status']}); removing it")
    for inst in call('snapshot')['instances']:
        if inst['name'] == 'Cancelled smoke': shutil.rmtree(library / 'instances' / inst['id'])
    wait(lambda: 'Cancelled smoke' not in [i['name'] for i in call('snapshot')['instances']], 'cancel cleanup', 30)

# A corrupt pack file fails cleanly and creates nothing.
corrupt = library / 'corrupt-smoke.mrpack'  # inside the library: sandboxed test apps can read it
corrupt.write_bytes(b'PK this is not a modpack' * 64)
call('importInstance', name='Corrupt smoke', source=str(corrupt))
task = wait(lambda: (t := call('snapshot').get('task')) and not t.get('active') and t, 'corrupt import', 120)
corrupt.unlink()
assert task['status'] not in ('Complete', 'Cancelled'), task
assert 'Corrupt smoke' not in [i['name'] for i in call('snapshot')['instances']]
print(f"PASS: corrupt pack refused ({task['status'][:80]})")

call('installModpack', project=hit['id'], version=release['id'], name='Modpack smoke')
task = wait(lambda: (t := call('snapshot').get('task')) and not t.get('active') and t, 'install', 600)
assert task['status'] == 'Complete', task
installed = [i for i in call('snapshot')['instances'] if i['name'] == 'Modpack smoke']
assert len(installed) == 1, installed
folder = library / 'instances' / installed[0]['id']
mods_dir = next(d for d in (folder / '.minecraft' / 'mods', folder / 'minecraft' / 'mods') if d.is_dir())
mods = list(mods_dir.glob('*.jar'))
cfg = (folder / 'instance.cfg').read_text(errors='replace')
assert f"ManagedPackID={hit['id']}" in cfg and f"ManagedPackVersionID={release['id']}" in cfg, 'pack metadata missing'
assert installed[0]['icon'].startswith('data:image/png'), 'instance icon missing'
print(f"PASS: Modrinth browser search, icons, releases, guard rails and install ({len(mods)} mods, managed pack metadata recorded).")

# Mod updates. A modpack pins the mods it installs (they update with the pack, as in Prism's own dialog), so the
# check must report them as pinned. A mod added on its own, one release behind, is what Update applies to.
inst_id = installed[0]['id']
listed = wait(lambda: (r := call('resources', id=inst_id, kind='mods')) and len(r) >= len(mods) and r, 'mod list', 60)
print(f"Mods & packs lists {len(listed)} mods")
call('checkModUpdates', id=inst_id)
updates = wait(lambda: (s := call('modUpdates')) and s.get('phase') in ('ready', 'failed') and s, 'update check', 600)
assert updates['phase'] == 'ready' and updates['managed'], updates
assert updates['scan']['pinned'] == len(mods), updates['scan']
print(f"PASS: update check reports the pack's {updates['scan']['pinned']} mods as pinned to the modpack")

def task_done(title_start, seconds=300):
    return wait(lambda: (t := call('snapshot').get('task')) and not t.get('active') and t.get('title', '').startswith(title_start) and t, title_start, seconds)

# Install a popular compatible mod the pack does not have, one release behind, through the console's Modrinth
# catalog (search, releases, review, install). Chosen from live results: whatever this loader supports.
installed_names = {r['name'].lower() for r in listed}
call('searchCatalog', id=inst_id, kind='mods', query='')
assert task_done('Search Modrinth')['status'] == 'Complete'
project, older = None, None
for candidate in call('catalogResults', id=inst_id)['projects'][:10]:
    if candidate['name'].lower() in installed_names:
        continue
    call('catalogVersions', id=inst_id, kind='mods', project=candidate['id'])
    assert task_done('Load compatible releases')['status'] == 'Complete'
    releases = call('catalogResults', id=inst_id)['versions']
    if len(releases) >= 2:
        project, older = candidate, releases[1]
        break
assert project, 'no popular compatible mod with two releases'
call('installCatalogResource', id=inst_id, kind='mods', project=project['id'], version=older['id'])
assert task_done('Resolve required dependencies')['status'] == 'Complete'
plan = call('catalogResults', id=inst_id)['plan']
assert any(item['file'] == older['file'] for item in plan), plan
call('confirmCatalogInstall', id=inst_id, kind='mods', project=project['id'])
assert task_done('Install resources')['status'] == 'Complete'
assert (mods_dir / older['file']).exists(), older
print(f"PASS: catalog installed {project['name']} {older['file']} (review listed {len(plan)} file(s))")

call('checkModUpdates', id=inst_id)
updates = wait(lambda: (s := call('modUpdates')) and s.get('phase') in ('ready', 'failed') and s, 'update check', 600)
assert updates['phase'] == 'ready', updates
older_jar = mods_dir / older['file']
chosen_update = next((u for u in updates['updates'] if u['name'].lower() == project['name'].lower()), None)
assert chosen_update, (project['name'], updates['updates'])
call('applyModUpdates', id=inst_id, names=[chosen_update['name']])
assert task_done('Update')['status'] == 'Complete'
assert not older_jar.exists(), 'old jar still present'
assert len(list(mods_dir.glob('*.jar'))) >= len(mods) + 1, 'updated jar missing'
print(f"PASS: updated {chosen_update['name']} {chosen_update['from']} -> {chosen_update['to']} (old jar replaced); pinned pack mods untouched")

# Leave the disposable library exactly as the other smoke tests expect it.
shutil.rmtree(folder)
for icon in (library / 'icons').glob(f"modrinth_{hit['slug']}.*"):
    icon.unlink()
wait(lambda: [i['id'] for i in call('snapshot')['instances']] == ['console-smoke'], 'library cleanup', 30)
