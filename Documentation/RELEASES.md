# Releases

Publishing a GitHub release starts `.github/workflows/release.yml`. On
standard GitHub runners, with no account, login or paid service, it builds:

* **the playable game** for Linux, Windows and macOS:
  `TacticalFPS-<tag>-<os>-<arch>` (`.tar.gz`; `.zip` on Windows). It holds
  the Godot client, a local server, lobby and bots, and the Play / Practice
  launchers. Built by `game.yml`, which also runs on every push to `main`
  (artifacts kept for 7 days);
* **server tool archives** `tfps-<tag>-<os>-<arch>`: dedicated server,
  lobby, bots and the headless C++ client;
* `SHA256SUMS` for all files.

Everything is uploaded only when every build and smoke test passed.

## Playing the download

Unpack the archive and start a launcher:

| Launcher | What it does |
|----------|--------------|
| `Practice.sh` / `Practice.bat` | Starts a local server and 4 combat bots that find, aim at and shoot enemies, then the game, connected. |
| `Play.sh` / `Play.bat` | Starts a lobby service on this computer and the main menu: host a lobby and share the invite code, or join one. |
| `Play.sh join HOST` | Uses a friend's lobby (the computer running Play). |
| `Play.sh practice harbor 6` | Map and bot count. |

**Starting the game directly:**
- The game itself (`Game/TacticalFPS.x86_64`, `Game/TacticalFPS.exe` or
  `Game/TacticalFPS.app`) also works on its own.
- Its Practice and Host menu entries start the bundled server, bots and
  lobby.

**The launchers:**
- They stop the server, lobby and bots when the game closes.
- Logs go to `Logs/`.

**Over the internet:** the host forwards TCP 7700 and the match UDP ports,
and sets `TFPS_PUBLIC_HOST` to their public address.

**Unsigned builds:**
- macOS: open with right-click > Open, or run
  `xattr -dr com.apple.quarantine TacticalFPS-*`.
- Windows: SmartScreen may ask for confirmation.

## How the game is built (`game.yml`)

Each OS runner (`ubuntu-latest`, `windows-latest`, `macos-latest`):

1. downloads the pinned official Godot (`Scripts/godot/GODOT_VERSION`) and
   its export templates from github.com/godotengine/godot releases (cached);
2. builds the native extension (`Scripts/godot/build_extension.sh`: CMake
   with GCC, MSVC or Apple Clang, universal binary on macOS) and the Rust
   server, lobby and bots;
3. exports the game (`Scripts/godot/export_game.sh`) and packs the download
   (`Scripts/package_game.sh`);
4. on Linux, runs two tests:
   * the **packaged real game** headless through its Practice launcher. It
     passes when the server logs the game client joining the match with
     both bots (`Scripts/smoke_test_game.sh`);
   * the native extension test against a live server.

**Verified:**
* The Linux build and both tests pass in CI and locally.
* The Windows build and package succeed in CI. Nobody has played it on
  Windows yet.
* The macOS export needed the ETC2/ASTC texture setting, which is fixed. It
  has not been played on a Mac yet.

## Cutting a release

1. Make sure `main` is green in CI.
2. On GitHub: *Releases → Draft a new release*, create a tag such as
   `v0.2.0` on `main`, write the notes, and click *Publish release*.
   (CLI: `gh release create v0.2.0 --target main --generate-notes`.)
3. The *Release* workflow runs (about 10–20 minutes). When every platform's
   build and smoke test has passed, the `publish` job uploads the files to
   the release. If any platform fails, nothing is uploaded.
4. To rebuild an existing release (for example after a runner hiccup):
   *Actions → Release → Run workflow* and enter the tag. Files are
   re-uploaded with `--clobber`, so they replace the old ones. The workflow
   builds the commit the tag points to, not `main`.

Draft releases do not start the workflow; publishing them does.

## Server tool archives

| File | Built on |
|------|----------|
| `tfps-<tag>-linux-x86_64.tar.gz` | `ubuntu-latest` (needs a glibc at least as new as that image's) |
| `tfps-<tag>-windows-x86_64.zip` | `windows-latest` (MSVC) |
| `tfps-<tag>-macos-arm64.tar.gz` | `macos-latest` (Apple Silicon) |
| `SHA256SUMS` | checksums of all of the above |
| `TacticalFPS-<tag>-linux-x86_64.tar.gz`, `-windows-x86_64.zip`, `-macos-universal.tar.gz` | the playable game downloads (see above) |

There is no Intel macOS archive: GitHub is retiring its Intel macOS runner
images. Intel Macs can build from source (`Scripts/package_release.sh`).

Each archive unpacks to one folder, `tfps-<tag>-<os>-<arch>/`:

| Content | What it is |
|---------|------------|
| `tfps-server` | authoritative dedicated server (UDP) |
| `tfps-lobby` | lobby service: invite codes, custom matches (HTTP API); runs its matches in-process |
| `tfps-bot` | headless Rust bot client(s) (`Tools/DevClient`) |
| `tfps_cli_client` | headless C++ client built from the client core (`Client/NetCoreTests`) |
| `tfps_render_sounds` | writes every procedural game sound as WAV files (`tfps_render_sounds <output folder>`). It is packaged when the tagged commit's CMake project has this target (all commits since the procedural audio work); otherwise the packaging script prints a note and skips it |
| `Shared/Definitions/` | `maps/*.json`, `weapons.json`, `gadgets.json`: what the server, lobby and clients load at runtime |
| `run_server`, `run_lobby`, `run_bot`, `run_cli_client` | start scripts: `.sh` on Linux/macOS, `.ps1` on Windows. They change to the package folder first, so the relative definition paths resolve from anywhere |
| `README.txt` | how to run it, ports, unsigned-binary notes |
| `VERSION.txt` | tag, commit, platform |

Binaries are not code-signed. On macOS run `xattr -dr com.apple.quarantine .`
in the unpacked folder once; on Windows SmartScreen may ask for confirmation.

## Building an archive locally

```bash
Scripts/package_release.sh v0.0.0-local          # → Dist/release/tfps-v0.0.0-local-<os>-<arch>.tar.gz
Scripts/smoke_test_package.sh Dist/release/tfps-v0.0.0-local-linux-x86_64.tar.gz
```

`package_release.sh` reuses `package_server.sh` (Rust binaries and
definitions, with `PACKAGE_DIR` pointing into the release folder) and
`build_netcore.sh` (CMake, into `build/release-netcore`), then adds the run
scripts from `Scripts/release/` and makes the archive. On Windows run it
from Git Bash. It makes a `.zip` with `7z`, or Python's `zipfile` if 7z is
missing. The smoke test for that `.zip` is `Scripts\smoke_test_package.ps1`.

## What is verified

On every platform the workflow tests the **packaged archive itself** before
anything is uploaded (`Scripts/smoke_test_package.sh`, and
`Scripts/smoke_test_package.ps1` on Windows):

1. unpack it into a temporary folder;
2. start the server with the packaged `run_server` script on a free UDP port
   and the lobby with `run_lobby` on a free TCP port, both on `127.0.0.1`
   (explicit loopback, because of the macOS wildcard-socket quirk noted in
   `Server/tests/multiplayer.rs`), from a different working directory;
3. `GET /health` on the lobby must answer `{"ok":true,...}`. The lobby only
   answers after it has loaded every map, weapon and gadget definition;
4. the packaged C++ client (`run_cli_client`) and Rust bot (`run_bot`) must
   both connect to the server (`connected as player`), and the server and
   lobby must still be running afterwards;
5. stop everything.

Verified on Linux by hand (x86_64): the archive was built with
`package_release.sh`, and the smoke test passed. It also failed as it should
on a copy of the archive with `gadgets.json` removed. The workflow passes
`actionlint` (with shellcheck). The macOS and Windows paths of the scripts
and workflow have **not** run yet. The first real release is their first
test, so check that run.

Not verified by the smoke test: gameplay beyond connecting, lobby match
start over the API, behaviour on machines other than the CI images (for
example older Linux distributions).
