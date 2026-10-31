<p align="center">
  <img src="console-ui/steamos/io.github.CoolFreeze23.PrismConsole.svg" alt="" width="128">
</p>

<h1 align="center">Prism Console</h1>

<p align="center">
  A controller-first Minecraft launcher for the Steam Deck and other SteamOS handhelds.<br>
  Install modpacks, manage mods and play, all from Gaming Mode, without touching the desktop.
</p>

> **Unofficial.** Prism Console is a fork of [Prism Launcher](https://prismlauncher.org). It is **not** Prism
> Launcher and is **not** endorsed by or affiliated with the Prism Launcher project. Please report problems
> here, not to Prism Launcher. Minecraft is a trademark of Mojang; this project is not affiliated with Mojang
> or Microsoft.

## What it does

- **Made for a controller.** Every screen works with A / B / X / Y, the D-pad and the sticks, with button
  prompts for Xbox, PlayStation and Nintendo layouts. No mouse, no desktop, no tiny Qt windows.
- **Modpacks in a few presses.** Browse and install Modrinth modpacks, check for mod updates, and turn mods,
  resource packs and shaders on or off.
- **Play straight in.** Jump into a world or a saved server directly. The launch screen shows each step and
  says what is happening if something is slow; if a launch fails you get a plain reason, Try again and Show log.
- **Game and launcher, side by side.** Hold **View + Menu** for a second to switch between a running game and
  the launcher (for example to check a log or stop the game).
- **Steam tiles.** Add any modpack to Steam as its own tile with generated artwork (optional, see below).
- **Your Prism skills still apply.** It is Prism Launcher underneath: instances, accounts, Java management
  and every advanced page are still there.
- English and Brazilian Portuguese.

## Install on a Steam Deck / SteamOS

> **Coming soon:** the first ready-to-install release is waiting for Microsoft sign-in approval for this project.
> Until it appears under [Releases](https://github.com/CoolFreeze23/prism-console/releases), the command below
> will report that it can't download Prism Console; you can [build it yourself](#building-it-yourself) meanwhile.

1. Switch to **Desktop Mode** (Steam button > Power > Switch to Desktop).
2. Open **Konsole** (it's in the application menu under System) and paste this line, then press Enter:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/CoolFreeze23/prism-console/main/install.sh | sh
   ```

3. Switch back to **Gaming Mode**. Prism Console is in your library under **Non-Steam**.

That's it. To update later, run the same line again. The installer checks the download's checksum and installs
the app for your user only (it never asks for your password).

**Want each modpack as its own Steam tile?** Run the installer with one extra option instead, then restart Steam:

```bash
curl -fsSL https://raw.githubusercontent.com/CoolFreeze23/prism-console/main/install.sh | sh -s -- --enable-steam-tiles
```

Steam only accepts new tiles from its own interface, so this turns on Steam's local debug interface
(`127.0.0.1:8080`, reachable only from the Deck itself). Prism Console uses it to add the tile and its artwork.
Leave it off if you prefer; everything else works without it. To turn it off again, delete
`~/.local/share/Steam/.cef-enable-remote-debugging` and restart Steam.

**Uninstall:** `curl -fsSL https://raw.githubusercontent.com/CoolFreeze23/prism-console/main/install.sh | sh -s -- --uninstall`
(your instances and worlds stay in `~/.var/app/io.github.CoolFreeze23.PrismConsole` unless you add `--delete-data`).

Prefer to do it by hand? Download `prism-console.flatpak` from the
[latest release](https://github.com/CoolFreeze23/prism-console/releases/latest) and run
`flatpak install --user prism-console.flatpak`.

## Using it

| Button | Does |
|---|---|
| **A** | Select |
| **B** | Back (one level at a time) |
| **X** | Configure the selected instance |
| **Y** | Mods & packs |
| **LB / RB** | Switch tabs |
| **Menu** | All Prism tools |
| **View + Menu (hold 1 s)** | Switch between the game and the launcher |

Outside Gaming Mode (Desktop Mode, or any other Linux desktop) the app opens as the normal Prism Launcher
window. Use **Open in Console Mode** from its menu entry to get the controller interface on the desktop.

**Controls inside Minecraft:** Minecraft itself has no controller support. Install a controller mod such as
[Controlify](https://modrinth.com/mod/controlify) in your instance, or use a Steam Input layout.

## Building it yourself

See [console-ui/README.md](console-ui/README.md) for the architecture, tests and development setup. In short:

```bash
git clone https://github.com/CoolFreeze23/prism-console.git && cd prism-console
git submodule update --init libraries/libnbtplusplus
(cd console-ui && corepack enable && pnpm install --frozen-lockfile && pnpm build)
flatpak-builder --user --install --install-deps-from=flathub --force-clean build flatpak/io.github.CoolFreeze23.PrismConsole.yml
```

Builds have Microsoft sign-in turned off unless you give them a Microsoft client ID of your own; see
[console-ui/docs/MICROSOFT-SIGN-IN.md](console-ui/docs/MICROSOFT-SIGN-IN.md).

## Credits and license

Prism Console is built on [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher) (GPL-3.0-only),
itself a fork of MultiMC. Controller navigation is adapted from
[X Minecraft Launcher](https://github.com/Voxelum/x-minecraft-launcher) (MIT), and the look follows the
Ore UI Prism theme by Nursultan Akim (MIT). Full details are in
[console-ui/UPSTREAM.md](console-ui/UPSTREAM.md).

All code is available under the [GPL-3.0-only](LICENSE) license; see [COPYING.md](COPYING.md) for the notices
of bundled components.
