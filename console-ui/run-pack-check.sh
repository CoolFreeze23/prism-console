#!/bin/sh
# Real-data launch check on the handheld: the installed Prism Console on a private Xvfb display with the UI
# debug port on 127.0.0.1:9223, started the way a Steam tile starts it. The environment is the one Steam gave
# a real tile launch (captured by capture-steam-env.sh; LC_ALL=C, Steam's LD_PRELOAD, PATH and Steam*/SDL
# variables), minus anything that would reach the real gamescope screen or session bus. The app runs through
# the installed /app/bin/prism-console wrapper (only its test-hostile unset line is dropped), so the wrapper's
# own fixes are what gets tested.
# Test only: PACK_CHECK_WITHOUT_LOCALE_FIX=1 also drops the wrapper's UTF-8 locale lines, to prove the launch
# check fails without them.
set -eu
root=/home/deck/Applications
steam_env=$root/steam-tile-env
[ -s "$steam_env" ] || { echo "No captured Steam environment; run capture-steam-env.sh first" >&2; exit 1; }
: > "$root/prism-console-pack-display"
LD_LIBRARY_PATH="$root" "$root/Xvfb-test" -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$root/prism-console-pack-display" >"$root/prism-console-pack-xvfb.log" 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
attempt=0
while [ ! -s "$root/prism-console-pack-display" ]; do
 kill -0 "$xvfb_pid"
 attempt=$((attempt + 1)); [ "$attempt" -lt 40 ] || exit 1
 sleep 0.5
done
display=$(cat "$root/prism-console-pack-display")

# Steam's tile environment, keeping the display, bus and session pointing at this test.
set --
while IFS= read -r line; do
 case "$line" in
  DISPLAY=* | WAYLAND_DISPLAY=* | GAMESCOPE_* | DBUS_SESSION_BUS_ADDRESS=* | XAUTHORITY=* | NOTIFY_SOCKET=* | INVOCATION_ID=* | JOURNAL_STREAM=* | MANAGERPID=* | SYSTEMD_EXEC_PID=* | STEAMVIDEOTOKEN=* | Steam3Master=* | "") ;;
  [A-Za-z_]*=*) set -- "$@" "$line" ;;
 esac
done < "$steam_env"

strip='/^unset /d'
[ "${PACK_CHECK_WITHOUT_LOCALE_FIX:-}" = 1 ] && strip="$strip; /LC_ALL\\|LC_CTYPE/d"
exec env -i "$@" DISPLAY=":$display" dbus-run-session flatpak run --env=DISPLAY=":$display" --env=QT_QPA_PLATFORM=xcb --env=PRISM_CONSOLE=1 \
 --env=QTWEBENGINE_REMOTE_DEBUGGING=127.0.0.1:9223 --env=PACK_CHECK_STRIP="$strip" --command=sh io.github.CoolFreeze23.PrismConsole \
 -c 'sed "$PACK_CHECK_STRIP" /app/bin/prism-console > /tmp/prism-console-pack-check; exec sh /tmp/prism-console-pack-check'
