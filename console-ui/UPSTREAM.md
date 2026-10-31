# Source provenance

Prism source base: PrismLauncher/PrismLauncher, commit
`573665ffa1ccbd7e19df39c28193d26d5b202d60` (GPL-3.0-only, with upstream notices).

Controller navigation and Big Picture base styling are copied from
Voxelum/x-minecraft-launcher pull request 1790, head
`ba4e1edb4d46e6828f224727aeb98f81fb53596b` (MIT).
The complete MIT notice is in `src/upstream/LICENSE`.

Copied files:
- `xmcl-keystone-ui/src/composables/gamepad.ts` → `src/upstream/gamepad.ts`
- `xmcl-keystone-ui/src/views/bigPicture.css` → `src/upstream/bigPicture.css`

App.vue adapts the header, library hero, instance collection and footer structure
of AppBigPictureView.vue / BigPictureLibrary.vue. XMCL services, accounts,
Minecraft installation and filesystem storage are intentionally not imported.
The Qt bridge operates on Prism's own live application models.

Local changes to gamepad.ts: remove the duplicate synthetic click after
HTMLElement.click(), which otherwise executes each controller action twice.
Steam keyboard requests are routed to the restricted native bridge instead
of attempting to open a new browser window.

SteamOS controller reconnect reconciliation: polls connection presence every 500 ms to recover controllers already present before VueUse listeners mount. Axis/button processing remains in the shared RAF driver.
