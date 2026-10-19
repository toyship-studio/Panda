# Working Agreements

- Prioritize the architecture and product as a whole over isolated fixes. Keep shared contracts and ownership clear; refactor when it simplifies the overall design. If a request requires substantial extra complexity, stop and discuss the approach instead of adding a workaround.
- Use only read-only Git commands. Do not stage, commit, stash, reset, checkout, restore, clean, or otherwise change Git state. Preserve existing staged and unstaged work; another thread may be editing the same workspace.
- Verify changes by building only. Do not launch applications, run test executables, start debugging sessions, automate UI, or capture screenshots unless the user explicitly asks. The user checks runtime and visual behavior and provides feedback.
