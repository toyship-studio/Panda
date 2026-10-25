# Writing a Panda plugin

A plugin extends the editor with data: a folder with a manifest, `plugin.json`, and a `data/`
folder whose subfolders say what the plugin provides. Plugins belong to the user, not to a
project — one installed plugin serves every project on the machine — and they never reach a game.
They are installed, updated and removed from the editor's Plugins tab (Window → Plugins), and
every change takes effect at once, without a restart.

A plugin carries no code. Engine systems are modules (see [Writing a module](../modules/README.md)),
and gameplay is the Bamboo scripts of a project; a plugin is what is left — files the editor reads.
Keyboard layouts are not plugins either: they are editor settings. Project templates and sample
projects are not planned as plugins for now.

## What a plugin can provide

| Folder | Provides | Page |
|---|---|---|
| `data/themes/*.json` | editor themes: the colors of the whole editor UI | [Themes](themes.md) |
| `data/icons/*.png` | editor icons: a replacement for any icon of the editor by its name | [Icons](icons.md) |
| `data/strings/…` | editor localization — planned, the format is not final; it comes once the editor UI goes through `tr()` | |

One plugin may provide several kinds at once (a theme with matching icons). The Plugins tab and
the catalog show what a plugin provides as tags, and the catalog filters by it.

## Starting from the template

The quickest start is the template repository
[toyship-studio/panda-plugin-template](https://github.com/toyship-studio/panda-plugin-template):
press **Use this template** on GitHub, clone the new repository and run `node tools/init.mjs`
(Node 20 or newer, nothing to install). It asks for the name, your GitHub login (the id becomes
`io.github.<login>.<slug>`), what the plugin provides, the license and the author, and writes
`plugin.json`, the data folders, `README.md`, `CHANGELOG.md` and `LICENSE`. The repository comes
with the validator, a publish script, a `Check` workflow for every push and a `Publish` workflow
for version tags, the format reference in `context/format.md`, and instructions for coding agents
(`AGENTS.md`, a Claude Code skill). The rest of this page is what the template does for you, so a
plugin can also be made by hand.

## Developing a plugin locally

1. Make a folder with `plugin.json` (the manifest below) and at least one data file, say
   `data/themes/my-theme.json` — or run `node tools/init.mjs` in a copy of the template.
2. In the editor open Window → Plugins and press **Install Dev Plugin**; pick the folder. The
   editor links it as an installed plugin (a symbolic link in its plugins folder; the folder stays
   where it is) and its card appears at the top of the list, marked **Dev**. A theme shows up as a
   button on the card: press it to apply.
3. Edit the files and press **Reload** on the card: the folder is read again and the applied theme
   is applied anew. Icons change on the spot as well.
4. A folder that is not a plugin (no `plugin.json` with an id, `parts` other than `["data"]`, an
   `engine` range this editor is outside of) is refused with a message. A theme file that cannot be
   read is skipped, and an unknown color name is a warning: both go to the Output panel.
5. **Uninstall** on a dev card removes only the link. If the catalog has a plugin with the same id,
   its card says "Overridden by a dev plugin" until the dev one is removed.

The plugins folder of the editor is `~/Library/Application Support/Panda/Plugins` on macOS,
`%APPDATA%\Panda\Plugins` on Windows and `~/.local/share/Panda/Plugins` on Linux (`$XDG_DATA_HOME`
when set). Installed plugins live there as folders named by their id; a dev plugin is a link with
the same name.

## Directory structure

```
my-theme/                   any folder name; the identity is the id in the manifest
  plugin.json               the manifest (required)
  data/                     the data (required): one subfolder per kind
    themes/
      panda-sunset.json
    icons/
      save.png
  README.md                 the long description: the plugin page on the portal
  CHANGELOG.md              what changed in each version, shown on the portal
  LICENSE                   LICENSE, LICENSE.txt or LICENSE.md
  icon.png                  the icon of the plugin itself (PNG, at most 1 MB): the card and the page
  screenshots/              png, jpeg or webp, at most 5 MB each, at most eight
```

Nothing else is accepted at the root, and nothing but kind folders under `data/`. The archive
must not contain symbolic links, executables or libraries (`.exe`, `.dll`, `.so`, `.dylib`,
scripts), or paths that leave the folder.

## The manifest

```json
{
    "manifestVersion": 1,
    "id": "io.github.yourname.sunset",
    "name": "Sunset",
    "version": "1.0.0",
    "description": "A warm dark theme with matching icons.",
    "license": "MIT",
    "engine": "^0.1.0",
    "parts": ["data"],
    "authors": ["Your Name <you@example.com>"],
    "category": "themes",
    "tags": ["theme", "dark", "warm"],
    "links": {
        "repository": "https://github.com/yourname/panda-sunset",
        "issues": "https://github.com/yourname/panda-sunset/issues"
    },
    "unlisted": false
}
```

| Field | Required | Meaning |
|---|---|---|
| `manifestVersion` | yes | the format version: `1` |
| `id` | yes | a reverse domain of at least three lowercase segments (`[a-z0-9-]`, dots between). Your namespace on the portal is `io.github.<login>.*` once a GitHub account is linked; other domains are given to verified publishers. The id never changes: it names the folder, the page and the setting that remembers a chosen theme |
| `name` | yes | the name in the list, at most 120 characters |
| `version` | yes | semantic version `major.minor.patch`, optionally `-beta.1`. A published version is immutable: every upload is a new version |
| `description` | yes | one line for the card, at most 500 characters; the long text is `README.md` |
| `license` | yes | an SPDX identifier such as `MIT` |
| `engine` | yes | the engine versions the plugin is made for, as a range: `^0.1.0` (any 0.1.x), `>=0.1.0 <0.3.0`, `0.1.x`, `*`. The catalog shows only plugins with a version that fits the running editor, and the editor refuses to install or link one that does not |
| `parts` | yes | always `["data"]` |
| `authors` | | `"Name <mail>"` strings; shown on the card ("by …") |
| `category` | | one lowercase word for the category chips of the Plugins tab: `themes`, `ui`, `editor`, `tools`, `localization` fit plugins |
| `tags` | | at most ten lowercase words (`[a-z0-9-]`, up to 32 characters), searchable |
| `links` | | `repository`, `homepage`, `issues`: `https://` addresses; the repository link is on the card |
| `unlisted` | | `true` keeps the plugin out of the catalog; it still installs by id |

## Checking the archive

The archive is a zip with the plugin folder as its single root entry, at most 50 MB. In a copy of
the template:

```
node tools/validate-plugin.mjs . --zip plugin.zip
```

checks the folder against the rules of this page, packs it (only `plugin.json`, `data/`,
`README.md`, `CHANGELOG.md`, `LICENSE*`, `icon.png` and `screenshots/` go in; the tools and the
workflows of the repository are left out) and exits with 1 and the list of problems — one line
per problem, with the same codes the portal reports — when something is wrong. By hand, from the
parent folder: `zip -r sunset.zip my-theme -x '*.DS_Store'`. The portal checks every upload the same
way before it publishes anything, and the Publish page shows the problems with their paths.

## Publishing

1. Sign in on [pandaengine.dev](https://pandaengine.dev) (a code by email) and link your GitHub
   account in the profile: that gives you the `io.github.<login>.*` namespace, and the id of the
   plugin must be inside it.
2. Open Account → Publish, drop the archive. It is checked first; **Publish** unlocks when the check
   passes. The plugin is in the catalog and in the Plugins tab of every editor within a minute.
3. To publish from the repository, make a publish token under Account → Tokens (scope `publish`)
   and add it to the repository secrets as `PANDA_PUBLISH_TOKEN`. The `Publish` workflow of the
   template runs on every `v*` tag: `git tag v1.0.0 && git push --tags` checks the archive and
   uploads it. From your machine the same is

   ```
   PANDA_PUBLISH_TOKEN=pnd_… node tools/publish.mjs .
   ```

   and without the tools it is one request — `POST /api/v1/packages/validate` with the same body
   only checks:

   ```
   curl -F archive=@sunset.zip -H "Authorization: Bearer pnd_…" https://pandaengine.dev/api/v1/packages
   ```

### Updating

Raise `version`, add the entry to `CHANGELOG.md`, build the archive and publish it again. Editors
with automatic updates on (the default) install the new version in the background and apply it at
once; the others show "Update available" on the card. A version that turned out broken is yanked
from Account → My plugins: it leaves the latest, the editor stops offering it, and anyone who has
it keeps it.

### What the editor does with a published plugin

When a user presses Install, the editor asks the portal for the latest version that fits its
engine version, downloads the archive and checks its size and SHA-256 against the portal's record,
checks the archive (one root folder, nothing executable), reads `plugin.json` (the id and the
version are the requested ones, `parts` is data, the `engine` range holds), and only then replaces
the installed folder. Themes and icons of the new version apply at once.

## Checklist

1. A copy of the template, `node tools/init.mjs` — or `plugin.json` by hand: id in your namespace,
   semver version, `engine`, `parts: ["data"]`, name, description, license.
2. At least one data folder: `data/themes/`, `data/icons/`.
3. Tested as a dev plugin in the editor: applied, reloaded, uninstalled.
4. `README.md` and `CHANGELOG.md`; `icon.png` 256×256; a screenshot or two.
5. The archive checked, uploaded on the Publish page, published.
