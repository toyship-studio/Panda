# Contributing

Panda is developed in the open by Toyship Studio. Issues, discussions and pull requests go to this
repository, including changes to the libraries that get read-only mirrors of their own (PandaUI,
Rain, Fern, Miren).

## Before you start

- [README.md](README.md) has the build and the repository layout; [`context/`](context/README.md)
  explains how the engine is built inside and why. The pages under `context/` are the design notes:
  read the one for the subsystem you touch.
- [`context/roadmap.md`](context/roadmap.md) and the Help wanted page on pandaengine.dev list where
  a hand helps most: example projects and demos, editor themes and plugins, tutorials and video
  reviews.
- For anything larger than a fix, open an issue or a discussion first and describe the change. The
  architecture as a whole matters more than any one feature; a change that needs a workaround is
  worth a conversation before the code.

## Pull requests

- One change per pull request, with a title that says what changed and why.
- Build what you touched: the editor and `PandaSmoke` (`scripts/build_panda_debug.sh`), the SDK
  package if the SDK or a module changed, and the configuration without modules if the core changed
  (`-DPANDA_MODULES=OFF`).
- Format C++ with `scripts/format.sh` and keep the style of the surrounding code.
- Update the docs that describe what you changed: `context/*.md` for the engine,
  `PandaSDK/docs/` for the SDK book, `README.md` of a module for its public surface.
- Do not add a dependency without a discussion. A vendored library comes with its license file, a
  row in `THIRD_PARTY_LICENSES.md` and an entry in `scripts/third_party_notices.py`; run the script
  to regenerate the notices.

## License of contributions

Every contribution is licensed under the terms of the code it joins: the MIT License in
`Foundation`, `Fern`, `Miren`, `Rain` and `PandaUI` (the `LICENSE.txt` of each folder), the Panda
Engine License of the repository (`LICENSE.txt`, section 5) everywhere else. There is no
contributor license agreement to sign. By opening a pull request you confirm that the contribution
is yours to give under those terms.

The names and the marks of the project are not part of the license: see "License" in README.md.
