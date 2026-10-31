#!/bin/sh
# Installs or updates Prism Console on SteamOS (or any Linux with Flatpak) and adds it to Steam.
#   curl -fsSL https://raw.githubusercontent.com/CoolFreeze23/prism-console/main/install.sh | sh
# Options:
#   --enable-steam-tiles  also lets the app add each modpack to Steam as its own tile (see the README first)
#   --uninstall           removes the app (instances and worlds are kept unless you add --delete-data)
set -eu

app=io.github.CoolFreeze23.PrismConsole
repo=CoolFreeze23/prism-console
release=${PRISM_CONSOLE_RELEASE:-https://github.com/$repo/releases/latest/download}

say() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31mError:\033[0m %s\n' "$*" >&2; exit 1; }

command -v flatpak >/dev/null || fail "Flatpak is not installed. On SteamOS it is built in; on other systems see https://flatpak.org/setup/"

steam_tiles=0
[ "${1:-}" = --enable-steam-tiles ] && steam_tiles=1

if [ "${1:-}" = --uninstall ]; then
  say "Removing Prism Console"
  flatpak uninstall --user -y --noninteractive "$app" || true
  if [ "${2:-}" = --delete-data ]; then rm -rf "$HOME/.var/app/$app"; say "Deleted its instances, worlds and settings"; fi
  say "Done. If you added it to Steam, remove the Prism Console shortcut from your library."
  exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

say "Downloading the latest Prism Console"
curl -fL --progress-bar -o "$work/prism-console.flatpak" "$release/prism-console.flatpak" \
  || fail "Could not download Prism Console. Check your internet connection and try again."
curl -fsSL -o "$work/prism-console.flatpak.sha256" "$release/prism-console.flatpak.sha256" \
  || fail "Could not download the checksum file."
(cd "$work" && sha256sum -c --quiet prism-console.flatpak.sha256) \
  || fail "The download is damaged (checksum mismatch). Run the installer again."

say "Installing (the first time this also fetches the KDE runtime from Flathub, about 500 MB)"
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y --noninteractive --reinstall "$work/prism-console.flatpak"

desktop="$HOME/.local/share/flatpak/exports/share/applications/$app.desktop"
if command -v steamos-add-to-steam >/dev/null && [ -f "$desktop" ] && ! grep -qs "$app" "$HOME/.local/share/Steam/userdata"/*/config/shortcuts.vdf; then
  say "Adding Prism Console to Steam"
  steamos-add-to-steam "$desktop" >/dev/null 2>&1 || say "Could not add it to Steam automatically; see below."
fi

if [ "$steam_tiles" = 1 ]; then
  # Steam only accepts new tiles from its own interface. This switch opens Steam's local debug interface
  # (127.0.0.1:8080, this device only) so Prism Console can add them; delete the file to turn it off again.
  steam_dir="$HOME/.local/share/Steam"; [ -d "$steam_dir" ] || steam_dir="$HOME/.steam/steam"
  touch "$steam_dir/.cef-enable-remote-debugging"
  say "Per-pack Steam tiles enabled. Restart Steam (or reboot) once for it to take effect."
fi

say "Prism Console is installed."
cat <<'DONE'

  Next:
  1. Go back to Gaming Mode and open Prism Console from Library > Non-Steam.
     (Not there? In Desktop Mode open Steam, choose Games > Add a Non-Steam Game, and pick Prism Console.)
  2. Sign in with your Microsoft account under Accounts, then install a modpack or add an instance.
  3. Optional: give each modpack its own Steam tile with Configure > Add to Steam
     (run the installer once with --enable-steam-tiles first; the README explains what that switch does).

  Hold View + Menu for a second to switch between a running game and the launcher.
  To update later, run this installer again.

DONE
