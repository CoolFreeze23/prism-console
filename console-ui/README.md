# Prism Console: developer guide

How the controller interface is built into Prism Launcher, how to build and test it, and how it is packaged.
For installing and using Prism Console, see the [main README](../README.md).

Controller frontend integrated into Prism Launcher and installed as the separate Prism Console app.
The native process is Prism Launcher; the visible console interface is Vue,
using controller navigation and styling from XMCL PR #1790. No XMCL backend
or PiLauncher backend is used. See UPSTREAM.md for exact revisions and notices.

The console renderer uses an adaptation of the Ore UI – Dark Emerald Prism theme: original nine-slice button assets,
charcoal surfaces, emerald actions, square panels, and high-contrast focus.
It is a bundled web adaptation, not a Qt stylesheet loaded into WebEngine.
Original theme assets/notices are in `src/oreui`; adaptation is `src/oreui.css`.

## Build

1. In this directory, `pnpm install --frozen-lockfile`, `pnpm test`, `pnpm build`.
2. Initialize Prism's `libraries/libnbtplusplus` submodule.
3. Build Prism with its usual dependencies plus Qt 6.8+ WebEngineWidgets and
   WebChannel, adding `-DLauncher_CONSOLE_UI=ON` to CMake.
4. Run `PRISM_CONSOLE=1 prismlauncher --dir /path/to/separate-test-data`.

Java **17** is required to compile Prism's legacy launch helper; Java 21 is
appropriate for running modern Minecraft, but cannot compile `-source 7`.
Containerfile supplies an isolated Linux compiler environment. Build the UI
first; the container embeds its dist directory. The container compilation is
not a portable SteamOS package.

The feature flag defaults off. Without PRISM_CONSOLE, the normal Prism window
still works. Do not point an experimental build at the live instance directory
until it has passed native integration tests. Distribution builds must supply
appropriate application branding and their own platform API credentials;
the container deliberately leaves the CurseForge API key empty.

## Implemented surface

- Real Prism instance library and icons, search, account selection.
- Microsoft device-code sign-in through Prism AuthFlow, with codes shown only
  in the local Accounts page (needs a Microsoft client ID, see docs/MICROSOFT-SIGN-IN.md).
- Vanilla creation and ZIP/MRPACK/HTTPS imports through Prism staging tasks,
  with a controller file browser, task status/progress and cancellation.
- Launch through Application/LaunchController, preserving Prism's downloads,
  authentication, Java selection and pre/post-launch hooks.
- Launch-in-progress guard and explicit confirmation for game termination.
- Instance name, memory, Java paths/arguments, window/performance defaults and
  command-hook overrides through Prism SettingsObject; Java detection through
  JavaInstallList. Commands are saved for Prism's normal launch lifecycle.
- Minecraft/NeoForge/Forge/Fabric/Quilt version selection through PackProfile
  and metadata lists, with loader-family conflict checks.
- Mods, resource packs and shaders through Prism ResourceFolderModel, with
  explicit idempotent enabled/disabled state rather than blind toggles.
- Local resource archive installation with duplicate-file protection; native
  Modrinth search, compatible versions, dependency review and explicit install.
- World copies, instance notes, optional-world ZIP export, and censored launch
  logs. Account reauthentication notices, offline-name entry, demo consent,
  JVM warnings and launch-stop prompts use the console surface.
- XMCL gamepad focus/navigation/button labels, keyboard navigation, scoped
  modal focus, and Steam keyboard action.
- In-process QWebChannel communication: no HTTP control server, token export,
  or remote page access. Explicit command-hook editing is a local privileged
  launcher feature; it is not a generic renderer shell-execution endpoint.

Controller-native onboarding now preserves existing Java preferences or enables
automatic Java downloads explicitly. Account management includes skin upload,
skin reset, owned-cape selection and typed-name sign-out; changes use Prism's
native requests. Instance copying excludes worlds unless selected. No live
account changes were made during validation.

## Extended Prism workflows

`ConsoleWorkflow` adapts Prism's existing dialogs, menus, settings pages and
instance editor into structured controls rendered by `NativeWorkflow` and
`WorkflowControl`. Qt retains the underlying models and behavior; its windows
stay off-screen and nonmodal so WebEngine remains input-capable. This is a
semantic adapter, not screen capture or a separate implementation of providers.

The surface includes full launcher settings, provider catalogs/import dialogs,
instance component/resource/world/server operations, nested file pickers,
confirmation prompts and original actions. Inline library rename has a console
dialog. Skin previews have controller rotation, zoom and outer-layer controls.
Secrets are masked and never serialized into the workflow snapshot. List actions
verify the displayed row before acting on asynchronously changing results.

External links and explicitly requested external tools still open their normal
applications. The renderer is adapted from XMCL's controller layouts; it does
not include XMCL's backend. Original Prism remains available separately.

## Packaging

`../flatpak/io.github.CoolFreeze23.PrismConsole.yml` is the Flatpak build (KDE 6.10 runtime, Qt WebEngine base
app). GitHub Actions (`../.github/workflows/build.yml`) builds the interface, runs its tests, builds the Flatpak
on every push, and publishes `prism-console.flatpak` with `install.sh` for every `v*` tag. `steamos/` holds the
entry point (`prism-console`: environment, UTF-8 locale under Gaming Mode's `LC_ALL=C`, `--console`), the
desktop entry, the icon and the AppStream metadata.

## Validation

The Linux C++ build passed with Qt WebEngine and Java 17. A headless native
smoke test passed against a disposable Prism library: actual QWebChannel
connection, Minecraft version display, image loading, instance settings
read/write, rejection of invalid IDs/routes, real mod disable/re-enable, and
idempotent repeated disable. It does not use any live instance data.
See `native-smoke.sh` and `native-smoke.py`. An isolated SteamOS Flatpak test
also passed on the handheld with Qt WebEngine's sandbox enabled. Remote
debugging is loopback-only and confined to disposable test data. Docker's
root-only test disables Chromium sandboxing; handheld tests do not.

Seventeen renderer tests cover connection failure, native error propagation, a hung
native process, stable-ID launch dispatch, starting-state protection and stop
confirmation, explicit imports/settings saves and opt-in world exports.
Headless browser checks also passed at 1280x800 and 800x600,
including modal focus restoration and absence of horizontal page overflow.
Controller checks include an already-connected pad, stick input, disconnect and
reconnect. Native task tests cover shared metadata tasks and completion races.
`workflow-smoke.py` exercises real settings/provider/instance pages, nested
dialogs and action dispatch against the disposable library.

Handheld gamepad navigation and real Minecraft launches (to the title screen, through the UI) are verified on
SteamOS with `run-pack-check.sh` and `artifacts/ui-launch-check.py`. Live skin/cape mutations and sustained
gameplay are not part of the automated checks.

## Handheld test packaging

`build-steamos.sh` builds inside the KDE 6.10 SDK with QtWebEngine BaseApp and
OpenJDK 17. `package-handheld-test.sh` creates the separate, no-menu-export
`io.github.CoolFreeze23.PrismConsole.Test` app with access only to disposable smoke data.
`run-handheld-check.sh` uses a virtual X display and private D-Bus session;
`handheld-check.sh` sets test-only software rendering and loopback debugging.
None of these test flags should be copied into a production launcher command.

The real-data launch check runs the installed app the way a Steam tile starts it. `capture-steam-env.sh`
opens the Prism Console tile once and saves the environment Steam gave it (`LC_ALL=C`, Steam's overlay
`LD_PRELOAD`, its `PATH` and the Steam/SDL variables) to `~/Applications/steam-tile-env`. Then
`run-pack-check.sh` starts the app under that environment, on a private Xvfb display and D-Bus session,
through the installed `/app/bin/prism-console` wrapper. `artifacts/ui-launch-check.py` plays CrazyCraft
through the UI and exits non-zero on any failure. It also requires a non-ASCII file name in the instance,
because Gaming Mode's `LC_ALL=C` once made CrazyCraft's pre-launch loader exit with code 1 on such a file.
`PACK_CHECK_WITHOUT_LOCALE_FIX=1` removes the wrapper's UTF-8 locale lines, to show that the check then fails.

## Console screens

Every screen is controller-first: A selects, B steps back one level (a chosen pack, an open screenshot)
before it leaves the page, LB/RB switch tabs, Menu opens all Prism tools, and button prompts follow the
connected pad (Xbox/Deck, PlayStation, Nintendo).

- **Library**: instances sorted by last played; the newest opens as "Continue playing" with playtime.
  X opens Configure, Y opens Mods & packs.
- **Launch screen**: shown from Play until the game window exists, with live status, progress, Hide,
  Show log and Cancel. A failed launch shows the reason with Try again. In Gaming Mode the launcher
  then steps aside so the game fills the screen, and returns when the game stops.
- **Browse modpacks**: Modrinth search (24 per page, Load more), pack details and releases (newest full
  release preselected), install through Prism's own importer with pack metadata and icon. Network,
  rate-limit and missing-project errors show a message with Try again. Icons are fetched once and cached.
- **Mods & packs**: enable/disable switches per mod, resource pack and shader; Check for updates
  identifies mods on Modrinth by hash, lists newer releases with switches, and updates all or a chosen
  few. Instances installed from a modpack show a warning first.
- **Worlds, servers & screenshots**: worlds sorted by last played, Play this world (quick play), Copy world;
  saved servers with Join, Add, Edit and Remove (asks first; locked while the game runs, because Minecraft
  rewrites the list when it exits; an unreadable servers.dat is never overwritten); a paginated screenshot
  gallery with a full-size viewer. Worlds and screenshots are never deleted here.
- **Configure**: name, memory, versions and loaders, Add to Steam / Update Steam tile, copy, export, notes.
- **Settings**: Java, memory & performance; Launcher settings (interface sounds and volume, mod
  dependencies and metadata, modpack update prompt, game-file download on create, parallel downloads,
  retries, timeout, play time, low-memory warning, About); language.
- **Interface sounds** are synthesized in the page with Web Audio (no audio files): navigate, confirm, back,
  error and launch. Quiet by default (35%), on/off and volume in Launcher settings, and silent whenever the
  page is hidden, i.e. while the game has the screen.

Rare admin pages stay on the generic adapter (the real Qt page, re-rendered with switches, segmented
choices and a fixed OK/Cancel bar), reached from Settings → Advanced Prism settings, Configure → Full
instance editor, or All Prism tools:

- launcher settings outside the list above: API keys and services, proxy, folder paths, Qt themes and
  appearance, external tools and profilers, custom commands, environment variables;
- the raw instance editor pages (component list, library order, custom jars, other logs, game options);
- modpack providers other than Modrinth (CurseForge, FTB, ATLauncher, Technic) and their managed-pack page;
- mods from providers other than Modrinth (Mods & packs → Other providers).

Test coverage: `pnpm test` (renderer), `browser-check.mjs` and `preview-shots.mjs` (fixture screenshots),
and on the hub `native-smoke.sh`, which runs `native-smoke.py`, `workflow-smoke.py` and `modpack-smoke.py`
against a real Prism with a throwaway library.

The View + Menu chord was agreed with the CrazyCraft controller scheme (crazycraft-controller-design):
in game, View and Menu act only on a solo tap, so holding both never also triggers an in-game action.

## Known issues

- Cancelling a modpack import during Prism's final pack-creation step can still finish the import, after
  which Prism logs a late "aborted" (an upstream race; a debug-build assertion, harmless in release builds).
  The console reports the real outcome.
- The View + Menu chord reads Steam Input's virtual pad. With Steam Input turned off for a game, the physical
  pad is reserved by Steam and cannot be read, so the chord does not work there.
