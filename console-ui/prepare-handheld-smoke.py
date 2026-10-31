"""Creates test data only, never opens the user's Prism data directory."""
from pathlib import Path
import json
import zipfile

root = Path('/home/deck/Applications/prism-console-smoke-data')
root.mkdir(exist_ok=True)
assert not (root / 'accounts.json').exists(), 'Refusing to overwrite an authenticated library'
(root / 'prismlauncher.cfg').write_text('''[General]
Language=en_US
IgnoreJavaWizard=true
AutomaticJavaDownload=true
AutomaticJavaSwitch=true
UserAskedAboutAutomaticJavaDownload=true
ApplicationTheme=system
IconTheme=flat
PastebinURL=
''')
instance = root / 'instances/console-smoke'
(instance / '.minecraft/mods').mkdir(parents=True, exist_ok=True)
(instance / 'instance.cfg').write_text('[General]\nInstanceType=OneSix\nname=Controller test\niconKey=default\n')
(instance / 'mmc-pack.json').write_text(json.dumps({'formatVersion': 1, 'components': [{'uid':'net.minecraft','version':'1.21.1','important':True}]}))
with zipfile.ZipFile(instance / '.minecraft/mods/smoke.jar', 'w') as archive:
    archive.writestr('fabric.mod.json', json.dumps({'schemaVersion':1, 'id':'console_smoke','name':'Console smoke','version':'1.0'}))
print('Disposable handheld library prepared')
