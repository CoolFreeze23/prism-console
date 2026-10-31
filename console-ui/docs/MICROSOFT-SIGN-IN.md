# Microsoft sign-in for Prism Console builds

Signing in to Minecraft goes through Microsoft, and every launcher identifies itself to Microsoft with a
*client ID*. Prism Launcher's client ID belongs to the Prism Launcher project, and Prism asks forks with code
changes not to use it, so Prism Console has its own. A build without a client ID works, but has Microsoft
sign-in turned off.

The official releases get the project's client ID from the `MSA_CLIENT_ID` repository secret. If you build
Prism Console yourself and want sign-in, use a client ID of your own, set up as below.

## 1. Register an application with Microsoft (about 10 minutes, free)

1. Sign in at <https://entra.microsoft.com> (or <https://portal.azure.com>) with a Microsoft account. A free
   account is enough.
2. Go to **Applications > App registrations > New registration**.
   - **Name:** Prism Console (or your fork's name).
   - **Supported account types:** *Personal Microsoft accounts only*.
   - **Redirect URI:** leave empty.
   - Select **Register**.
3. On the app's **Overview** page, copy the **Application (client) ID**. That is your client ID.
4. Open **Authentication**, scroll to **Advanced settings**, set **Allow public client flows** to **Yes**, and
   save. (The console signs in with a device code, the "go to microsoft.com/link and enter this code" flow,
   which needs this.)

## 2. Ask Mojang for Minecraft API access

New client IDs cannot sign in to Minecraft until Mojang approves them. Until then, sign-in fails with an
"invalid app registration" style error.

1. Fill in Mojang's Minecraft API access form:
   <https://aka.ms/mce-reviewappid> (it asks for the client ID, the app's name, a description and a link, for
   example this repository).
2. Approval is by email and can take from a few days to a few weeks.

## 3. Use it

- **GitHub releases (this repository):** add the client ID as a repository secret named `MSA_CLIENT_ID`
  (Settings > Secrets and variables > Actions > New repository secret). The next tagged build carries it.
- **Your own Flatpak build:** replace `@MSA_CLIENT_ID@` in `flatpak/io.github.CoolFreeze23.PrismConsole.yml`
  with the client ID before running `flatpak-builder`.
- **Plain CMake:** configure with `-DLauncher_MSA_CLIENT_ID=<client id>`.
- **The handheld developer deploy:** put `PRISM_CONSOLE_MSA_CLIENT_ID=<client id>` in
  `console-ui/local-build.env` (ignored by git).

Accounts signed in under one client ID must sign in again after the build switches to another.
