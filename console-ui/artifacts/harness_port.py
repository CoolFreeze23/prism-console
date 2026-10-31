# The UI debug endpoint of the handheld harness. run-pack-check.sh starts the app on the first free port from
# 9223 (other apps, Vesktop for one, can hold 9223) and writes the port it chose next to itself.
from pathlib import Path

PORT_FILE = Path('/home/deck/Applications/prism-console-pack-port')


def debug_targets():
    port = PORT_FILE.read_text().strip() if PORT_FILE.exists() else '9223'
    return f'http://127.0.0.1:{port}/json'
