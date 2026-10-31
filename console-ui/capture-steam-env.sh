#!/bin/sh
# Captures the environment Steam gives a non-Steam tile, for run-pack-check.sh. Opens the "Prism Console"
# tile (shortcut app id below) in Gaming Mode for a few seconds, reads the environment of Steam's reaper for
# that launch (what Steam hands the shortcut, before flatpak and the wrapper), then closes the app.
# Run on the handheld with nothing else open in Prism Console.
set -eu
appid=${1:?Usage: capture-steam-env.sh <app id of the Prism Console tile (Steam: Properties)>}
out=/home/deck/Applications/steam-tile-env
steam "steam://rungameid/$(( (appid << 32) | 0x02000000 ))" >/dev/null 2>&1 &
for _ in $(seq 1 40); do
 reaper=$(pgrep -f "[r]eaper SteamLaunch AppId=$appid " | head -1) && [ -n "$reaper" ] && break
 sleep 1
done
[ -n "${reaper:-}" ] || { echo "The tile did not start" >&2; exit 1; }
sleep 3
tr '\0' '\n' < "/proc/$reaper/environ" > "$out"
flatpak kill io.github.CoolFreeze23.PrismConsole 2>/dev/null || true
echo "Captured $(wc -l < "$out") variables to $out ($(grep -E '^LC_ALL=' "$out" || echo 'no LC_ALL'))"
