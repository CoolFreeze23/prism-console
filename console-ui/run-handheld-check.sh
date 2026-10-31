#!/bin/sh
set -eu
: > /home/deck/Applications/prism-console-display
LD_LIBRARY_PATH=/home/deck/Applications /home/deck/Applications/Xvfb-test -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>/home/deck/Applications/prism-console-display > /home/deck/Applications/prism-console-xvfb.log 2>&1 &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true' EXIT
attempt=0
while [ ! -s /home/deck/Applications/prism-console-display ]; do
  kill -0 "$xvfb_pid"
  attempt=$((attempt + 1))
  [ "$attempt" -lt 40 ] || exit 1
  sleep 0.5
done
display=$(cat /home/deck/Applications/prism-console-display)
test -n "$display"
DISPLAY=:$display dbus-run-session flatpak run --env=DISPLAY=:$display io.github.CoolFreeze23.PrismConsole.Test
