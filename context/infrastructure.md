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
store).

### The Plugins tab

A single dock tab opened by Window → Plugins. It is a passive view over the plugin store, like
every editor panel. Installing, applying and removing all happen here; there is no separate theme
menu.

- Header: a search field (requests are debounced), the filter All / Installed / Updates, and
  category chips by what a plugin provides (Themes now; languages and templates later).
- One card per plugin: icon, name, publisher, version, one-line description, download count,
  what it provides, a link to its page on the portal, and one action button.
- An installed plugin with themes lists them on its card, each with Apply; the active theme is
  marked.
- A details view: description, versions and the changelog as plain text. The README opens on the
  portal (PandaUI has no markdown renderer yet).
- Secondary actions: Install Dev Plugin..., Check for Updates, the auto-update toggle,
  Publish Plugin....
- Built-in plugins (shipped in the editor resources) are listed as installed and cannot be
  removed.
- Offline: installed plugins are always listed from the local folders; the catalog shows the
  cached index with a notice.

The button follows the status of the plugin:

| Status | Button |
|---|---|
| not installed | Install |
| installing, updating, removing | progress, disabled |
| installed | Remove; every theme of the plugin has Apply |
| update available | Update |
| incompatible with this engine version | disabled, with the reason |
| failed | the error text and Retry |
| dev plugin | Reload, Remove |

### The plugin store

`PandaEditor/src/Project/Plugins/PluginStore` holds the state and is the only code that changes
the plugin folders.

- Catalog: `GET /api/v1/packages?kind=plugin&q=&provides=&engine=<ENGINE_VERSION>&page=`, cached
  in `<userdata>/Panda/registry/index.json`.
- Installed plugins: `Resources/plugins/*` (built-in) and `<userdata>/Panda/Plugins/*`. One
  installed version per plugin; the folder name is the plugin id and `plugin.json` is the truth,
  so there is no separate list of installed plugins.
- One operation per plugin id at a time (install, update or remove); a second request for the
  same id is ignored, and the tab shows the operation as the status. Operations run on PandaAsync
  workers and their results are applied on the main thread.
- Install and update:
  1. download the archive through the API (the download is counted) into
     `<userdata>/Panda/Plugins/.staging/`;
  2. verify the size and the sha256 against the version metadata;
  3. unpack into a temporary folder inside `.staging`, rejecting paths that leave the folder,
     symbolic links and executables;
  4. validate `plugin.json`: the id and the version are the requested ones, `parts` is `data`,
     the `engine` range matches;
  5. swap: remove the old folder and rename the temporary one to `Plugins/<id>`;
  6. reload.
- Remove: unload the contributions of the plugin, delete its folder, reload.
- Updates: checked at startup and together with the engine update check,
  `GET /api/v1/packages/updates?ids=&engine=`. With auto-update on (the default) updates are
  installed in the background and applied live; otherwise the Updates filter shows them.
- Dev plugin: Install Dev Plugin... creates a symbolic link `Plugins/<id>` to the developer's
  folder. It is marked as dev, overrides the registry version with the same id and is never
  auto-updated. Reload rescans it. A file watcher can replace the button later.

### Reload without a restart

After every operation the store rescans the folders and compares the old and the new set of
plugins by id and version. Contributions of removed and changed plugins are unloaded, those of
new and changed plugins are loaded (the same diff as `extensions_updated` in Zed). Nothing else is
touched.

- Every kind of plugin data has a loader with `load` and `unload`. A kind of data is accepted into
  the plugin format only if it can be loaded and unloaded live.
- Themes are applied from the Plugins tab. If the active theme has changed, it is applied again;
  if it was removed, the editor falls back to the built-in default.
- There is no theme menu to rebuild. The View → Theme submenu that exists today is removed when
  the Plugins tab arrives; until then it stays, because it is the only way to switch a theme.
  Later themes are chosen from a command palette, as in Zed and VS Code (`context/roadmap.md`).

### What the engine needs first

None of these exists in the tree today, and the editor auto-update needs the same three:

- HTTP, the first part of the network layer of Fern (`Fern::Net`, `context/networking.md`): GET,
  POST, PUT, multipart, a download into a file with progress, resume and a SHA-256 check, on the
  network stacks of the systems, asynchronous through PandaAsync.
- ZIP unpacking: a small vendored library (miniz, MIT).
- SHA-256: a small implementation in Foundation; the network layer checks downloads with it while
  the bytes arrive.

Compatibility is the package's `engine` range against `ENGINE_VERSION`.

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
    tar.gz. No signing.
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

The model follows Zed (`crates/auto_update`): poll once an hour, compare with the running
version, download to a temporary location with progress, install in the background, show
"Restart to update". On macOS the dmg is mounted and copied over the running bundle with
`rsync -a --delete`; on Windows a helper executable waits for the application to exit, swaps the
files with a rollback and restarts it; on Linux the editor swaps the install directories itself
before it restarts.

Panda (`PandaEditor/src/App/EditorUpdater`):

- Check at startup and once an hour: read the channel manifest. The editor has two manifest
  addresses built in (our domain and the file in `panda-builds`) and tries the second when the
  first does not answer. It compares build numbers. Settings: `autoUpdate` (on or off) and the
  channel (`stable` or `preview`). Menu: Help → Check for Updates, Release Notes.
- Download: first through our download address `/dl/...?from=editor`, which counts the download
  and redirects to the file; when that fails, the direct `urls` of the manifest in order. The
  download resumes after a break (HTTP ranges) and the file is verified by sha256.
- The update check passes the current version, the platform and the outcome of the previous
  download (which storage served it, or the error), without any identifier.
- Install in the background into `<userdata>/Panda/updates/`:
  - macOS: mount the dmg, `rsync -a --delete` into the current `.app` (the signed bundle is
    replaced as a whole, so the signature stays valid), unmount. The editor's own files
    (`settings.json`, `session.json`) live in the user data directory, not in the bundle, so the
    swap does not touch them.
  - Windows: unpack the zip into `updates/` and restart through `Tools/PandaUpdateHelper.exe`,
    which waits for the editor to exit, moves the current install to `old/`, moves `install/`
    into place, rolls back on error and starts the new version. The install is per user
    (`%LOCALAPPDATA%\Panda\Editor`), so no administrator rights are needed.
  - Linux: unpack the tar.gz (the system `tar`) into `updates/` and swap it into the per-user
    install (`~/.local/share/Panda/Editor`, the same shape as on Windows) on restart: the editor
    moves the current install to `old/` and `install/` into place itself, since a running
    executable may be replaced on Linux, then starts the new version; `old/` is the rollback. An
    editor installed from a distribution package is updated by the distribution, and the updater
    only shows that a new version exists.
- UI: an "Update ready · Restart" indicator and the same action in Help. A click saves the state
  and restarts. Without a click the update applies on the next start.
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
