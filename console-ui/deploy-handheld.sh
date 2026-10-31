#!/bin/sh
# Build the current tree on the SteamOS handheld and update the installed Prism Console app.
# Developer tool. Run from the repository root on the PC: sh console-ui/deploy-handheld.sh deck@<handheld-ip>
# (or set PRISM_CONSOLE_HANDHELD). Prerequisite: console-ui/dist is built (pnpm build). Leaves the Steam
# shortcut, artwork and data untouched. A Microsoft client ID for sign-in is read from console-ui/local-build.env
# (PRISM_CONSOLE_MSA_CLIENT_ID=..., never committed); without it the build has Microsoft sign-in off.
set -eu
[ -f console-ui/local-build.env ] && . ./console-ui/local-build.env
target=${1:-${PRISM_CONSOLE_HANDHELD:-}}
[ -n "$target" ] || { echo 'Usage: sh console-ui/deploy-handheld.sh deck@<handheld-ip>'; exit 1; }
msa_client_id=${PRISM_CONSOLE_MSA_CLIENT_ID:-}
apps=/home/deck/Applications
ssh="ssh -o BatchMode=yes -o ConnectTimeout=8 $target"

test -f console-ui/dist/index.html || { echo 'Build the UI first: (cd console-ui && pnpm build)'; exit 1; }

# Never compete with a game or drain a low battery: a full build runs four compiler jobs.
$ssh 'set -eu
  if pgrep -x java >/dev/null; then echo "Minecraft is running; try again after closing it."; exit 1; fi
  # A charger can be online while a heavy game still drains the battery, so trust the mains supply first.
  charger=0
  for supply in /sys/class/power_supply/*; do
    [ "$(cat "$supply/type" 2>/dev/null)" = Mains ] && [ "$(cat "$supply/online" 2>/dev/null)" = 1 ] && charger=1
  done
  for bat in /sys/class/power_supply/BAT*; do
    [ -e "$bat/status" ] || continue
    if [ "$charger" = 0 ] && [ "$(cat "$bat/status")" = Discharging ] && [ "$(cat "$bat/capacity")" -lt 40 ]; then
      echo "Battery is at $(cat "$bat/capacity")% and discharging; plug in the handheld first."; exit 1
    fi
  done'

archive=$(mktemp)
trap 'rm -f "$archive"' EXIT
tar -czf "$archive" --exclude=.git --exclude=node_modules --exclude=cmake/vcpkg --exclude=console-ui/artifacts .
scp -q -o BatchMode=yes "$archive" "$target:$apps/prism-console-update.tar.gz"

$ssh "set -eu
  cd $apps
  # Extract over the existing tree so the incremental build directories (deps, steam-build) are reused.
  tar -xzf prism-console-update.tar.gz -C prism-console-source
  rm prism-console-update.tar.gz
  jdk=\$(ls -d \$HOME/.local/share/flatpak/runtime/org.freedesktop.Sdk.Extension.openjdk17/x86_64/*/*/files | head -n 1)
  # Keep the device awake when polkit allows it (SSH sessions often are not allowed), and build at
  # the lowest CPU and I/O priority so a running game keeps first claim on the machine.
  inhibit=
  systemd-inhibit --what=sleep:idle --why=Prism-Console-build true 2>/dev/null && inhibit='systemd-inhibit --what=sleep:idle --why=Prism-Console-build'
  \$inhibit nice -n 19 ionice -c 3 flatpak build --share=network --bind-mount=/usr/lib/sdk/openjdk17=\$jdk --env=PRISM_CONSOLE_MSA_CLIENT_ID=$msa_client_id prism-console-build \
    sh $apps/prism-console-source/console-ui/build-steamos.sh > prism-console-build.log 2>&1 \
    || { tail -n 30 prism-console-build.log; exit 1; }
  grep -q STEAMOS-BUILD-DONE prism-console-build.log

  package=prism-console-preview-package
  previous=prismlauncher.previous
  cp \$package/files/bin/prismlauncher \$previous
  flatpak kill io.github.CoolFreeze23.PrismConsole 2>/dev/null || true
  cp prism-console-build/files/bin/prismlauncher \$package/files/bin/prismlauncher
  # Entry point, Desktop Mode menu entry and icon (written as Flatpak exports, as build-finish would).
  steamos=prism-console-source/console-ui/steamos
  install -m 755 \$steamos/prism-console \$package/files/bin/prism-console
  # Exec stays in its source form (prism-console); flatpak build-export rewrites it to a flatpak run line.
  install -D -m 644 \$steamos/io.github.CoolFreeze23.PrismConsole.desktop \$package/export/share/applications/io.github.CoolFreeze23.PrismConsole.desktop
  install -D -m 644 \$steamos/io.github.CoolFreeze23.PrismConsole.svg \$package/export/share/icons/hicolor/scalable/apps/io.github.CoolFreeze23.PrismConsole.svg
  flatpak build-export prism-console-preview-repository \$package
  flatpak --user install -y --or-update prism-console-local-preview io.github.CoolFreeze23.PrismConsole
  installed=\$(flatpak info --show-location io.github.CoolFreeze23.PrismConsole)
  cmp prism-console-build/files/bin/prismlauncher \$installed/files/bin/prismlauncher
  sha256sum \$installed/files/bin/prismlauncher
  echo 'Installed. Previous binary kept at' $apps/\$previous"
