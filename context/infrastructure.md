# Infrastructure: CI, releases, auto-update and the Plugins tab

What lives in the engine repository: the release workflow, the documentation build, the editor
auto-update, the Plugins tab of the editor, and the contract the editor and the CI use against the
portal. The portal itself (`pandaengine.dev`, its API, the plugin registry, the admin, the studio
site, the server) and the launch plan (repositories, secrets, the order of work) are in the private
`panda-portal` repository: `docs/architecture.md`, `docs/api.md`, `docs/storage.md`,
`docs/deployment.md`, `docs/launch.md`.

## 1. Premise

- Engine builds on CI and automatic updates for users: an update is installed after a restart
  or with one click and an immediate restart.
- A plugin registry: authors upload packages, users install them from the editor without a
  restart.
- The documentation site is built from this repository.

The main audience is international, so downloads have to work everywhere. No single location
guarantees that: GitHub has been unreliable for users in Russia since May 2026, Cloudflare is
throttled there, and hosting in Russia is blocked by some foreign networks and has no CDN.
Builds are therefore published to two storages (section 4).

## 2. What the portal gives the engine

The contract is `panda-portal/docs/api.md`. The engine uses these parts of it:

- `GET /api/v1/releases/{channel}/latest` is the channel manifest (the same JSON as the file in
  the storage and in `panda-builds`); `GET /dl/{channel}/{os}/{arch}?from=editor` is the counted
  download that redirects to the file.
- `GET /api/v1/packages?kind=plugin&q=&provides=&engine=` is the catalog,
  `GET /api/v1/packages/updates?ids=&engine=` the update check of installed plugins,
  `GET /api/v1/packages/{id}/versions/{version}/download?from=editor` the counted download of an
  archive.
- `POST /api/v1/packages` publishes a plugin with a publish token from the account; the editor
  sends the zipped plugin folder. The archive rules (one root folder, `plugin.json`, `data/`
  only, no executables) are listed in `api.md` and checked by `tools/validate-plugin.mjs` of the
  portal.
- `POST /api/v1/feedback` takes problem and crash reports with attachments;
  `POST /api/v1/telemetry/events` takes opt-in daily aggregates.
- The release workflow talks to `POST /api/v1/releases`, `PUT .../assets/{os}/{arch}` and
  `POST .../publish` with a service token (`PANDA_REGISTRY_CI_TOKEN`).

Namespaces of plugin ids: `io.github.<login>.*` belongs to the account with the linked GitHub
login; other domains (`dev.pandaengine.*`, the author's own) belong to verified publishers. Sign-in
on the site is by a one-time code sent by email; GitHub is linked in the account.

## 3. Plugins in the editor

Requirement: plugins are found, downloaded, installed, updated and removed inside the editor, in
a dedicated Plugins tab, and take effect WITHOUT restarting the editor. The reference is the
Extensions page of Zed (`crates/extensions_ui` for the page, `crates/extension_host` for the
store). Code: `PandaEditor/src/Project/Plugins/PluginStore` (the store), `UI/Documents/PluginsDocument`
and `UI/Panels/PluginsView` (the tab).

### The Plugins tab

A document of the center dock (Window → Plugins, the "+" menu of the dock; the tab id is
`plugins`). It is a passive view over the plugin store: it reads the store every frame (the
store's revision grows with every change) and asks for operations. Installing, applying a theme
and removing all happen here; there is no theme menu (the APPEARANCE section of the editor
settings offers the same themes).

- Header: the title and Install Dev Plugin; the search field (requests are debounced by 300 ms)
  and the filter All / Installed / Not Installed; the category chips (All and the categories of the
  manifest table in `context/plugins.md`). The portal filters by `q` and `category`; the editor
  only pages.
- The list. All: the dev plugins first (as in Zed), then installed plugins the catalog does not
  list (built-in ones, or the registry is unreachable), then the catalog in portal order
  (downloads). Installed: the local set filtered by the search and the category. Not Installed:
  the catalog without the installed ids. Pages of 20 (`GET /packages?…&page=&size=20`) load as the
  list is scrolled near its end; a Load more button with "Showing N of M plugins" sits under the
  list.
- One card per plugin: the icon (`iconUrl` of the catalog cached in `registry/icons/<id>.png`, or
  the plugin's own `icon.png`, or its initial), the name, the latest version and the installed one
  when they differ, the Built in / Dev / Update available marks, the author or the owner, downloads
  and the update date, the description, tags for what it provides, the category, the manifest
  tags, the license, the engine range and the id, the themes of an installed plugin (one button
  each, the active one marked), the error of the last failed operation, links (the page on the
  portal, the repository, Copy ID) and the action buttons. Nothing on the page is set below 15 px.
- Empty states: "Loading plugins…", the failure line with Try again, "No plugins match your
  search.", "No installed plugins.", "Every plugin is installed."; when the portal does not answer
  and the saved catalog is shown, a line above the list says so.
- A problem that belongs to no card (Install Dev Plugin on a folder without plugin.json) is a
  message dialog.

The buttons follow the status of the plugin:

| Status | Buttons |
|---|---|
| not installed | Install |
| installing, updating, removing | the operation name, disabled; a progress bar under the header while downloading |
| installed, latest | Uninstall; every theme of the plugin has a button, the active one is marked |
| update available | Update to vX, Uninstall |
| built-in | none ("Built in") |
| dev | Reload, Uninstall ("Dev") |
| catalog entry whose id is installed as a dev plugin | Install, disabled ("Overridden by a dev plugin") |
| failed | the message in red; the buttons work again, the next attempt clears it |

### The plugin store

`Panda::PluginStore` holds the state and is the only code that changes the plugin folders. It is
owned by `EditorLayer` and reaches the tab through `EngineBridge` (`pluginStore()`,
`currentThemeId()`, `selectTheme(id)`, `installDevPlugin()`).

- Installed plugins: `Resources/plugins/*` (built-in) and `<userdata>/Panda/Plugins/<id>`,
  scanned by `EditorPlugins` (`InstalledPlugin {manifest, directory, builtIn, dev, themes, icons}`). One
  installed version per plugin; the folder name is the plugin id and `plugin.json` is the truth,
  so there is no separate list of installed plugins.
- Catalog: `GET /api/v1/packages?kind=plugin&sort=downloads&engine=<ENGINE_VERSION>&page=&size=20`
  plus `q` and `category` (`CatalogState {query, items, total, nextPage, loading, failed,
  fromCache, error}`). With `engine` the portal lists only packages that have a compatible
  version. The first page of the empty query is saved as `<userdata>/Panda/registry/index.json`
  and shown when the portal does not answer.
- Versions: `GET /api/v1/packages/updates?ids=&engine=` gives the latest compatible version of
  each id with its size and sha256. An install resolves its version through it, and the update
  check (at startup and after every change of the installed set) compares it with the installed
  version (`Project/Plugins/Semver`). With automatic updates on (`EditorUpdateSettings.autoUpdate`,
  the switch of the editor update) newer versions are installed at once.
- One operation per plugin id at a time (`PluginOperation {kind, version, progress}`); a second
  request for the same id is ignored. The network goes through `Fern::Net` (completions on the
  main thread), the file work runs on PandaAsync workers, and the result is applied on the main
  thread.
- Install and update:
  1. the version through the updates endpoint (no compatible version — the error on the card);
  2. `GET /api/v1/packages/{id}/versions/{version}/download?from=editor` (counted by the portal)
     into `<userdata>/Panda/Plugins/.staging/<id>-<version>.zip`, the size and the sha256 checked
     by the network layer while the bytes arrive;
  3. on a worker: the entry names are checked (one root folder, no entry outside it, no executable
     or library by extension), the archive is unpacked into `.staging/<id>-<version>/`, and
     `plugin.json` is validated (`manifestVersion` 1, the id and the version are the requested
     ones, `parts` is `["data"]`, the `engine` range holds `ENGINE_VERSION`);
  4. swap: the old folder is removed and the unpacked one renamed to `Plugins/<id>` (a dev link
     with that id refuses the install); the archive and the staging folder are deleted;
  5. rescan and notify.
- Remove: `remove_all` of `Plugins/<id>` on a worker; a dev plugin only loses its link (and its
  marker), the developer's folder stays. Built-in plugins cannot be removed.
- Dev plugin: Install Dev Plugin opens the folder dialog; the folder's `plugin.json` names the id
  (it must be a data plugin whose `engine` range holds this editor). Whatever has that id in
  `Plugins/` gives way, and a symbolic link `Plugins/<id>` → the folder is created (Windows without
  developer mode: a junction through `mklink /J`); a marker `Plugins/.dev/<id>` names the source
  and tells a junction from a folder. A dev plugin is never auto-updated; Reload rescans the
  folders.
- Icons: `icon(id, iconUrl, localFile)` returns the texture from the cache, or downloads the
  address into `registry/icons/<id>.png` and reports through the revision when it arrived; the
  textures are created by the UI host (`EditorUI::loadImage`) on the main thread.
- `.staging` is wiped at startup (a download or an unpack that did not finish).

### Reload without a restart

After every operation the store rescans the folders; when the set of ids and versions changed it
calls the host (`setOnInstalledChanged`), which applies the theme of the settings again: the files
of a changed plugin are read anew, a removed theme falls back to the built-in dark one (the
setting keeps the id, so a reinstall brings the theme back). Reload of a dev plugin notifies even
when the set did not change — its files did. The tab needs no notification: it rebuilds the cards
whose model changed on the next frame.

- Every kind of plugin data has to be loadable and unloadable live before it is accepted into the
  plugin format; today that is themes (`EditorPlugins::loadTheme`, `PandaUI::setTheme`) and icons
  (`EditorUI::applyIconOverrides`: the plugin PNG goes behind the texture handle the icon token
  already has — `PandaUIImplMiren::replaceTexture` — so every view showing it changes on the next
  frame; a token without an override goes back to the editor's file, a token the editor lacks
  becomes a new icon). The kinds and their state are listed in `context/plugins.md`, "Plugin data
  kinds".
- Later themes are also chosen from a command palette, as in Zed and VS Code
  (`context/roadmap.md`).

### What the engine needed first

All three exist now, and the editor auto-update (section 5) uses the same ones:

- HTTP, the first part of the network layer of Fern (`Fern::Net`, `context/networking.md`): GET,
  POST, PUT, multipart, a download into a file with progress, resume and a SHA-256 check, on the
  network stacks of the systems, asynchronous through PandaAsync.
- zip archives: `PandaEditor/src/Project/System/ZipArchive` on the vendored miniz (`Vendor/miniz`,
  MIT): unpacking with a check that no entry leaves its folder, one entry read without unpacking,
  the entry list, packing a folder for Publish; the Windows updater unpacks its build with it.
- SHA-256: `Foundation/Hash/Sha256`; the network layer checks downloads with it while the bytes
  arrive.

Compatibility is the package's `engine` range against `ENGINE_VERSION` (`Project/Plugins/Semver`:
the npm grammar of the portal's `SemverRange` — `1.2.3`, `^1.2.3`, `~1.2`, `>=1.0.0 <2.0.0`,
`1.x`, `*`, alternatives with `||`; the pre-release tag of the engine version is ignored, so
`0.1.0-preview.142` satisfies `^0.1.0`).

## 4. Engine CI and releases

Workflows in the engine repository:

- `ci.yml` (pull requests and main): macOS, Windows and Linux; configure, build (`PandaEditor`,
  `PandaSmoke`, `PandaSDKCorePackage`) and run `PandaSmoke` where the runner has a GPU (macOS;
  the Windows and Ubuntu runners have none, so they build only). Dependencies are cached (shader
  tools through `scripts/fetch_shader_tools`); the Linux job installs the packages of the README.
- `release.yml` (run by hand for `preview`; later by a tag `vX.Y.Z` for `stable`): three build
  jobs and a publish job. The work is in `scripts/release/`, so every step also runs on a
  developer machine:
  - Every build job configures with `scripts/configure_panda_release.*`, builds only the
    `PandaEditor` target and then the Release SDK (`scripts/build_panda_sdk_release.*`: a second
    tree without the editor API), so the SDK is inside every build. The iOS SDK packages stay out
    of the preview for now (`PANDA_SDK_IOS=0` for `build_panda_sdk_common.sh`; the switch leaves
    the workflow when iOS joins).
  - The version of every build is `ENGINE_VERSION` of `Panda/include/Panda/Base/Version.hpp`
    (edited by hand on a release; the root CMakeLists reads it for the bundles and the manifest).
    The build number and the commit are `-DPANDA_BUILD_NUMBER=<run number>` and
    `-DPANDA_BUILD_COMMIT=<sha>` of the run, in the code `ENGINE_BUILD_NUMBER` and
    `ENGINE_BUILD_COMMIT` (a local build is 0 without a commit); the About window shows them.
    `package.py` reads the header for the archive name.
  - macOS (`macos-14`, arm64): `sign_macos.sh` signs the tools inside `Contents/Resources`
    (slangc and its libraries, cmake) and then the bundle with the hardened runtime and
    `PandaEditor/native/OSX/PandaEditor.entitlements` (library validation off: the editor dlopens
    the script bundles it builds); `package.py` makes the dmg (`hdiutil`, an Applications link);
    the dmg is signed, notarized (`notarytool submit --wait`, the image covers the bundle inside)
    and stapled (`notarize_macos.sh`). Signing and notarization run only when the secrets are set;
    without them the run produces an unsigned dmg, enough to test the pipeline before the
    certificates exist.
  - Windows (`windows-2022`): Visual Studio 2022 Release build, the `PandaEditor/` layout of `bin/`
    (exe, resources, `Tools/`, `PandaSDK/`) as a zip. The SDK is copied by
    `cmake/sync_panda_sdk.cmake`, the same script that embeds it into the macOS bundle, so every
    platform carries the same SDK without its own `tools/` (the editor brings `Tools/`). Authenticode signing is optional (without it
    SmartScreen warns). The update helper (`Tools/PandaUpdateHelper.exe`) arrives with the updater.
  - Linux (`ubuntu-24.04` runner, `ubuntu:22.04` container): GCC 11 and the libraries of the
    README, the oldest system the editor supports, so the binary runs on Ubuntu 22.04 and newer.
    CMake is Kitware's portable build, pinned in the workflow: the cmake that configures the
    engine is the one bundled into the editor (`cmake/bundled_cmake.cmake`), and the dynamically
    linked cmake of the distribution would not run elsewhere. The same `PandaEditor/` layout as a
    tar.gz, plus `share/applications/dev.pandaengine.PandaEditor.desktop` and
    `share/icons/hicolor/512x512/apps/dev.pandaengine.PandaEditor.png` from `PandaEditor/native/Linux`
    (`package.py`). No signing. The install on Linux is the model of Zed: `curl -f
    https://pandaengine.dev/install.sh | sh` (the script lives in the portal frontend, `public/install.sh`)
    downloads the build of the channel through `/dl/<channel>/linux/<arch>?from=install`, unpacks it
    into `~/.local/panda-editor`, links `~/.local/bin/panda-editor`, writes the desktop entry with the
    absolute paths into `~/.local/share/applications` and the icon into the hicolor theme, and
    reminds about PATH; `sh -s -- --uninstall` removes all of it and keeps the user data
    (`~/.local/share/PandaEditor`). The windows carry the identity `PandaEditor` /
    `dev.pandaengine.PandaEditor` (`Fern::setDesktopIdentity`: the X11 class and the Wayland app id),
    so the launcher groups them under the icon. The updater keeps working: it swaps the install
    folder in place, so the link and the entry stay valid.
  - Publish (`ubuntu-24.04`): `publish.py` takes the archives of the three jobs (artifacts) and
    does the storage work of the next sections: uploads, manifests, retention, the portal
    registration; the job summary lists the addresses. At least one storage must be configured,
    and the manifests are not touched when no storage took a file. Files go to the object storage
    with the public-read ACL, as the portal backend uploads its public objects (the bucket also
    holds private ones), and `docs.yml` uploads the documentation the same way. The aws cli sends
    checksums only where the protocol requires them (`AWS_REQUEST_CHECKSUM_CALCULATION=when_required`):
    its default ones are rejected by many S3-compatible storages. It also takes the root
    certificates of the system (`AWS_CA_BUNDLE`): the list built into aws cli v2 lacks GlobalSign
    Root CA - R6, which signs the certificate of the storage, and the upload fails with
    "self-signed certificate in certificate chain".
- `docs.yml` (push to `main` that touches `PandaSDK/docs/` or the book config): builds the
  documentation site with mdBook (always, so a broken page fails the run) and uploads it to the
  object storage (below) when the storage secrets are set.
- Runners: standard GitHub-hosted runners (`macos-14` on Apple Silicon, `windows-2022`,
  `ubuntu-24.04`) are free for public repositories; larger runners are always billed. Code from
  external pull requests must never run on self-hosted machines. Backend and frontend deploy from
  GitHub-hosted Ubuntu over SSH. The same configure scripts, CMake targets and release scripts run
  on a developer machine and in CI.

### The preview channel is rolling

The engine is raw and is updated with small fixes almost daily, so `preview` keeps no history:

- The version is `0.1.0-preview.<build>`. The build number always grows, even when an older
  commit is rebuilt, so a fix after a rollback looks like a new version to the updater.
- Every build records its commit (in the manifest and in the About window). A rollback means
  rebuilding an older commit from Git; a report from a tester names the exact code.
- Preview builds are not tagged in the engine repository.
- Files carry the build number in their names: `PandaEditor-0.1.0-preview.142-macos-arm64.dmg`.
- The `stable` channel comes later and keeps history: versioned releases by tags, never deleted.
  Adding it changes only the workflow, not the editor.

### Two storages

Every build is uploaded to both places:

1. the object storage, prefix `builds/<channel>/`;
2. the rolling release `preview` of `toyship-studio/panda-builds` on GitHub.

Publishing order: upload the new files to both storages, then switch the manifests, then delete
the build before the previous one. A client therefore never sees a manifest that points to a
missing or half-uploaded file, and the previous build stays available for a manual download.
If one upload fails, the manifest lists only the storage that succeeded.

### Manifest

One JSON file per channel: `builds/<channel>/manifest.json` in the object storage (addressed
through the public base of the bucket: the endpoint and the bucket, or the repository variable
`S3_PUBLIC_BASE_URL`, named as in the server `.env` of the portal) and `manifests/<channel>.json` in `panda-builds` (a file in the repository,
not a release asset: release assets cannot be read by a script from another domain, files of a
repository can; the raw address of GitHub serves it). `publish.py` writes both after the files,
and sizes and hashes come from the archives it uploads, so a signed dmg is described as signed.

```json
{
  "channel": "preview",
  "version": "0.1.0-preview.142",
  "build": 142,
  "commit": "abc1234",
  "date": "2026-10-04",
  "notes": "Short list of changes",
  "files": [
    {
      "os": "macos", "arch": "arm64", "kind": "dmg", "size": 251000000, "sha256": "...",
      "urls": [
        "https://<bucket>/builds/preview/PandaEditor-0.1.0-preview.142-macos-arm64.dmg",
        "https://github.com/toyship-studio/panda-builds/releases/download/preview/PandaEditor-0.1.0-preview.142-macos-arm64.dmg"
      ]
    }
  ]
}
```

The editor and the Download page know only the manifest addresses and take the file addresses
from the manifest. Storage can therefore change without updating clients.

### Documentation

The documentation is a static site built from the engine repository, the way Zed builds
`zed.dev/docs` with mdBook. The content lives only in the repository; on our side there are only
the built files in the object storage, like the builds.

- Source: `PandaSDK/docs/*.md`, the same pages that ship inside the SDK. The mdBook
  configuration and the summary live next to them in the engine repository
  (`PandaSDK/book.toml` with `src = "docs"` and the output in `bin/docs`,
  `PandaSDK/docs/SUMMARY.md`); the theme is still to come.
- Build: `docs.yml` runs `mdbook build` on every push to `main` that touches the documentation
  and uploads the output to `site/docs/preview/`. With the `stable` channel the release
  workflow also publishes `site/docs/<version>/` and `site/docs/stable/` by tag, so the
  documentation follows the release channels like the builds.
- Serving: nginx of the portal serves `pandaengine.dev/docs/` from the storage prefix. The
  portal only links to it; the React application contains no documentation code.
- Theme: the mdBook theme follows the portal: the same header, fonts and colors. Search,
  navigation and the dark theme come from mdBook. Pages are ready HTML, so search engines and
  link previews see the documentation without any work on our side.
- Later: a Bamboo API reference generated from the SDK headers, published on the same site and
  shown as help inside the editor from the same source (the Godot way).

## 5. Editor auto-update

The model follows Zed (`crates/auto_update`): a check at startup and once an hour, the download
of a newer build in the background, the install in the background, "Restart to update". The code
is `PandaEditor/src/App/EditorUpdater` on the HTTP of `Fern::Net` (`context/networking.md`).

- **A build knows its channel and its number.** `ENGINE_RELEASE_CHANNEL` (`-DPANDA_RELEASE_CHANNEL`
  of the release workflow) and `ENGINE_BUILD_NUMBER`; a local build has neither, its updater is off
  and Help → Check for Updates says why. The channel is a property of the build, not a setting,
  as in Zed. The one setting is `autoUpdate` in the `updates` section of the editor scope (the
  UPDATES section of the settings document: the switch, the state line, Check for Updates).
- **The check** reads the channel manifest from the portal
  (`/api/v1/releases/<channel>/latest?from=editor&current=<build>&os=&arch=`, with the outcome
  of the previous download), and from `manifests/<channel>.json` in `panda-builds` when the portal
  does not answer. A build number above the running one is an update. A manual check shows "up to
  date" and errors; an automatic one stays quiet when it fails (offline), like Zed.
- **The download** goes to `<user data>/Panda/updates/<file>`: first through the counted address
  `/dl/<channel>/<os>/<arch>?from=editor` of the portal, then through the addresses of the manifest
  in order; the size and the SHA-256 of the manifest are checked by `Fern::Net`, a broken download
  resumes.
- **The install** runs on a worker of the engine scheduler and is the same on every platform:
  the new build is staged next to the user data, and the install folder is swapped when the
  editor exits, so the running editor never reads a file of the new version (the SDK inside the
  bundle, baked shaders and templates belong to the running binary until the restart).
  - Staging: macOS mounts the dmg and copies the bundle with `ditto` (every attribute of the signed
    bundle kept) into `updates/stage/PandaEditor.app`; Windows and Linux unpack the archive into
    `updates/stage/PandaEditor`.
  - The swap: the install folder is the one the editor runs from (the bundle, or the folder the
    archive was unpacked into: there is no fixed install path). On exit the current folder moves
    to `<folder>.old`, the staged one takes its place (a rename, or a copy across volumes), a
    failed move is rolled back, `.old` is removed (or on the next start). macOS and Linux do the
    swap themselves in `main()` after the teardown (`EditorUpdater::finishOnExit`), since the
    folder of a running executable may move there; Windows cannot, so `Tools/PandaUpdateHelper.exe`
    (built with the editor, copied to `updates/`) waits for the process to exit and does the same.
    All start the new editor when a restart was asked (`open -n` on macOS). A quit without the
    restart applies the update too, so the next start is the new version.
  - `updates/pending.json` records the staged build; the next start of that build shows "Updated
    to <version>" with the release notes one click away, and removes the leftovers.
- **UI.** One indicator (`UI/Panels/UpdateIndicator`) in the toolbar and on the start screen:
  checking, downloading with a progress bar, installing, "Update ready · Restart" (a click opens the
  dialog with the release notes of the manifest and Restart Now), "Updated to …", "Update failed ·
  Details"; a × hides it until the state changes (Zed's dismiss). Help: Check for Updates, Restart
  to Update (while one is installed), Release Notes (the Download page of the portal), About. About
  shows the version, the channel, the build and the commit, the platform and the updater's state,
  with Copy for a report and the two actions.
- **Restart** is the Quit flow with the unsaved-changes dialog, then `main()` starts the new
  instance (`SystemTools::relaunchOnExit`), or the helper does on Windows.
- After an update: script bundles are rebuilt against the new SDK (the editor prompts when a
  project was built with an older version); plugins are filtered by their `engine` range.
- A build that crashes at startup cannot update itself; the tester downloads the newest build by
  hand from the Download page or from `panda-builds`.

macOS requires Developer ID signing and notarization: without them Gatekeeper blocks the
downloaded bundle and auto-update is not practical. On Windows signing is desirable but does not
block updates. Linux needs no signing.

## 6. What the editor reports

Nothing the editor sends identifies a person.

- Every download goes through the counted address `/dl/...?from=editor` (and
  `/packages/.../download?from=editor` for plugins); a direct link from the manifest is used only
  when our address is unreachable, and such a download is not counted.
- The update check passes the running version and the platform (`from=editor&current=&os=&arch=`):
  the portal counts them per day as live installs.
- The update check also reports the outcome of the previous download
  (`downloadOutcome=ok|failed&downloadStorage=s3|github`), which shows failures per storage.
- Opt-in telemetry (asked on first start) sends daily aggregates: `editor_start`, `install`,
  `update`, `plugin_install`, `build_error` with the OS and the engine version.

## 7. Decisions

1. Apple Developer Program is available. Needed in the secrets of the engine repository: a
   Developer ID Application certificate (.p12 and password) and an App Store Connect API key for
   `notarytool` (Key ID, Issuer ID, .p8).
2. Authenticode signing of Windows binaries comes later; Azure Trusted Signing is the cheap
   option.
3. The public engine repository under the `toyship-studio` organization starts from a snapshot
   of the tree in one initial commit, without the history: the history carries files that cannot
   be redistributed. The private repository is kept as the archive with the full history.
4. Examples live in the public `PandaExamples` repository; the portal only links to them.
5. Engine builds go to two storages from the first release: the object storage and the public
   repository `panda-builds`. The fallback has to be in the editor in advance: an editor that
   cannot reach its only source cannot download the update that would teach it a second one.
6. The `preview` channel is rolling, without history; `stable` with history comes later.
7. Modules and libraries stay in the engine repository. The tree authoring tool of the studio is a
   separate private repository: it builds an engine checkout as its CMake subproject
   (`PANDA_PATH`), the engine repository carries nothing of it, and nothing of it is distributed.
   It is GPL-2.0-or-later because its growth model derives from The Grove, and a GPL binary that
   contains the engine would have to be licensable as a whole under the GPL, which the engine
   license is not.
8. Bundled fonts are Inter Display and JetBrains Mono (SIL OFL).
9. The documentation is a static site built by mdBook from `PandaSDK/docs/` by the engine CI and
   published to the object storage; the portal serves it under `/docs/` and only links to it.
10. Portal decisions (sign-in by email code, local administrators, object storage, download
    routing, one private repository, client-side rendering, English only) are recorded in
    `panda-portal/docs/architecture.md`.
11. The engine is licensed under the Panda Engine License 1.0 (`LICENSE.txt`, `NOTICE`), copyright
    Andreichev Mikhail, except the six libraries of decision 12, which are MIT. The text is derived from the Apache License 2.0, whose steward allows
    modified reuse as long as the result is not called Apache, with one added condition: the
    engine, the editor, the SDK and the runtime, original or modified, are not sold or otherwise
    commercialised as a game engine product. Games, extensions, free forks and paid services
    around the engine are unrestricted; the Marks (names, logos, app icons, mascots) are outside
    the Work. The model is the Defold License. Decided in October 2026, replacing the MIT draft:
    the studio earns on games and on work for partners, not on engine licenses, but a fork sold as
    a competing engine is not wanted; the sole owner can relax the license later, while code once
    published under MIT would stay MIT. The license is not OSI-approved, so the site says "free,
    source available", not "open source". Contributions come in under the same license without a
    CLA (`CONTRIBUTING.md`). The binaries carry `THIRD_PARTY_NOTICES.txt`, generated by
    `scripts/third_party_notices.py`; it opens with the engine's own notice and license text, so an
    exported game meets the notice conditions without any step of its developer. The GPL tree tool
    of the studio is outside the repository (decision 7).
12. One repository, no submodules. The libraries that are useful on their own, Foundation,
    PandaAsync, Fern, Miren, Rain and PandaUI, are licensed under the MIT License (the `LICENSE.txt` of each folder,
    one and the same text; `scripts/third_party_notices.py` checks it), decided in October 2026 so
    that they can be used anywhere; the game engine condition stays on the rest of the engine. They
    are made buildable standalone: `cmake -S Rain -B build` on that folder alone, the samples or
    examples and the smoke test built when the folder is the top-level project. Engine folders a
    library needs come through FetchContent of their mirrors and third-party code through
    FetchContent or `find_package`, both only when the engine targets are absent. The manual
    workflow `Mirrors` (`.github/workflows/mirrors.yml`, nothing runs on a push) builds each
    folder alone, as its mirror will see it, and only then publishes it with `git subtree split`
    into `toyship-studio/<Folder>`, the same names as the folders. It pushes with the
    `PANDA_MIRRORS_TOKEN` secret, or with `PANDA_BUILDS_TOKEN` when that fine-grained token also
    covers the mirror repositories (Contents: read and write). A library joins the workflow when its
    repository exists, in the order Rain, PandaUI, then Foundation, PandaAsync, Fern and Miren (Fern
    builds on Foundation and, for its network layer, on PandaAsync; Miren builds on Fern). Issues and pull requests go to the engine repository; the mirrors have Issues off.
13. PandaUI is complete like Dear ImGui: `backends/` and `examples/` live inside `PandaUI/` and
    are compiled only when the folder is the top-level project (`PANDAUI_BUILD_BACKENDS`,
    `PANDAUI_BUILD_EXAMPLES` default to `PROJECT_IS_TOP_LEVEL`). The engine backend (Miren and
    Fern) stays in `Panda`; `PandaUITest` remains the demo of that backend.
