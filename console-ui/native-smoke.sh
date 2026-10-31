#!/bin/sh
# This runs only in the disposable root-owned test container, not on SteamOS.
set -eu
pacman -S --noconfirm --needed python-websocket-client qt6-websockets qt6-imageformats >/build/native-smoke-deps.log 2>&1
# The smoke never needs Minecraft's game files: installs skip them (Prism fetches them at first launch), so the
# suite tests the console, not Mojang's CDN. Prism writes every setting back with its default, so the value is
# forced each run (an "only if missing" guard never fired). Mojang's game-file hosts are black-holed in this
# container, and any request to them fails the run with the URL instead of passing on a lucky download.
cfg=/build/console-smoke-data/prismlauncher.cfg
if grep -q '^DownloadGameFilesDuringInstanceCreation=' "$cfg" 2>/dev/null; then
  sed -i 's/^DownloadGameFilesDuringInstanceCreation=.*/DownloadGameFilesDuringInstanceCreation=false/' "$cfg"
else
  sed -i '/^\[General\]$/a DownloadGameFilesDuringInstanceCreation=false' "$cfg"
fi
grep -q '^DownloadGameFilesDuringInstanceCreation=false$' "$cfg"
game_hosts='piston-meta.mojang.com piston-data.mojang.com launchermeta.mojang.com launcher.mojang.com libraries.minecraft.net resources.download.minecraft.net'
grep -q 'smoke: no game files' /etc/hosts || echo "127.0.0.1 $game_hosts # smoke: no game files" >> /etc/hosts
no_game_downloads() {
  hits=$(grep -o -E 'Running "https?://(piston-meta\.mojang\.com|piston-data\.mojang\.com|launchermeta\.mojang\.com|launcher\.mojang\.com|libraries\.minecraft\.net|resources\.download\.minecraft\.net)[^"]*' "$1" | sort -u || true)
  [ -z "$hits" ] || { echo "FAIL: the smoke tried to download Minecraft game files:"; echo "$hits"; exit 1; }
  echo "PASS: no Minecraft game-file downloads ($(basename "$1"))"
}
Xvfb -displayfd 3 -screen 0 1280x800x24 3>/build/native-xvfb-display >/build/native-xvfb.log 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
sleep 1
DISPLAY=:$(cat /build/native-xvfb-display) PRISM_CONSOLE=1 QTWEBENGINE_DISABLE_SANDBOX=1 QTWEBENGINE_REMOTE_DEBUGGING=127.0.0.1:9222 QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu \
  /build/prismlauncher --dir /build/console-smoke-data >/build/native-launcher.log 2>&1 &
launcher_pid=$!
trap 'kill "$launcher_pid" "$xvfb_pid" 2>/dev/null || true' EXIT
python /src/console-ui/native-smoke.py

python /src/console-ui/workflow-smoke.py

python /src/console-ui/modpack-smoke.py
no_game_downloads /build/native-launcher.log

# Unreachable servers: restart Prism with a placeholder account and its traffic sent to a closed port,
# play, then put the library back.
kill "$launcher_pid"; wait "$launcher_pid" 2>/dev/null || true
python /src/console-ui/launch-failure-smoke.py prepare
trap 'kill "$launcher_pid" "$xvfb_pid" 2>/dev/null || true; wait "$launcher_pid" 2>/dev/null || true; python /src/console-ui/launch-failure-smoke.py restore' EXIT
DISPLAY=:$(cat /build/native-xvfb-display) PRISM_CONSOLE=1 QTWEBENGINE_DISABLE_SANDBOX=1 QTWEBENGINE_REMOTE_DEBUGGING=127.0.0.1:9222 QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu \
  /build/prismlauncher --dir /build/console-smoke-data >/build/native-launcher-failure.log 2>&1 &
launcher_pid=$!
python /src/console-ui/launch-failure-smoke.py

# A network that drops a connection: the first connection to Prism's metadata server stalls until Prism's
# timeout, Prism retries quietly, and New instance still lists Minecraft versions.
kill "$launcher_pid"; wait "$launcher_pid" 2>/dev/null || true
python /src/console-ui/launch-failure-smoke.py restore
: > /build/retry-proxy.log
python /src/console-ui/retry-smoke.py prepare
python /src/console-ui/retry-smoke.py proxy &
proxy_pid=$!
trap 'kill "$launcher_pid" "$xvfb_pid" "$proxy_pid" 2>/dev/null || true; wait "$launcher_pid" 2>/dev/null || true; python /src/console-ui/retry-smoke.py restore' EXIT
DISPLAY=:$(cat /build/native-xvfb-display) PRISM_CONSOLE=1 QTWEBENGINE_DISABLE_SANDBOX=1 QTWEBENGINE_REMOTE_DEBUGGING=127.0.0.1:9222 QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu \
  /build/prismlauncher --dir /build/console-smoke-data >/build/native-launcher-retry.log 2>&1 &
launcher_pid=$!
python /src/console-ui/retry-smoke.py
