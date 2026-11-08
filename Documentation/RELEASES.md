# Releases

Publishing a GitHub release starts `.github/workflows/release.yml`. It builds
runnable archives for Linux, Windows and macOS, starts each one in a smoke
test, and attaches the archives and `SHA256SUMS` to the release.

The archives hold everything that runs **without Unreal Engine**: the
dedicated server, the lobby service and the headless clients. The playable
Unreal game client is a separate, optional job that needs a self-hosted
runner with Unreal Engine; it is **untested** (see below).

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

## Archives

| File | Built on |
|------|----------|
| `tfps-<tag>-linux-x86_64.tar.gz` | `ubuntu-latest` (needs a glibc at least as new as that image's) |
| `tfps-<tag>-windows-x86_64.zip` | `windows-latest` (MSVC) |
| `tfps-<tag>-macos-arm64.tar.gz` | `macos-latest` (Apple Silicon) |
| `SHA256SUMS` | checksums of all of the above |
| `tfps-<tag>-client-<Win64\|Linux\|Mac>.zip` (+ `.sha256`) | only if the self-hosted Unreal job is set up (untested) |

There is no Intel macOS archive: GitHub is retiring its Intel macOS runner
images. Intel Macs can build from source (`Scripts/package_release.sh`).

Each archive unpacks to one folder, `tfps-<tag>-<os>-<arch>/`:

| Content | What it is |
|---------|------------|
| `tfps-server` | authoritative dedicated server (UDP) |
| `tfps-lobby` | lobby service: invite codes, custom matches (HTTP API); runs its matches in-process |
| `tfps-bot` | headless Rust bot client(s) (`Tools/DevClient`) |
| `tfps_cli_client` | headless C++ client built from the Unreal client's NetCore (`Client/NetCoreTests`) |
| `tfps_render_sounds` | sound renderer tool, **only** when the tagged commit's CMake project has that target (the packaging script copies it if it was built and prints a note otherwise) |
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

## Unreal game client (self-hosted runner) 🔒 untested

GitHub-hosted runners have no Unreal Engine, so the `unreal-client` job runs
only on your own machine. 🔒 **Nobody has run this job.** No Unreal Engine was
available when it was written, and the Unreal project has never been compiled
with UnrealBuildTool (see [STATUS.md](STATUS.md)). Expect to fix things on the
first run.

What it does: it checks out the tag and runs `Scripts/package_client.sh`
(Linux/macOS) or `Scripts\package_client.ps1` (Windows). That script calls
`RunUAT BuildCookRun` (Shipping, build/cook/stage/pak/archive) for one
client platform: Win64, Linux or Mac. There is one job per platform in
`TFPS_UNREAL_PLATFORMS`. Each job runs on a runner with the labels
`self-hosted`, `unreal` and the matching OS label (`windows`, `linux`,
`macos`). Self-hosted runners get the OS label automatically. The job then zips
`Dist/client-<platform>` as `tfps-<tag>-client-<platform>.zip`, writes a
`.sha256`, and attaches both to the release with
`softprops/action-gh-release@v2`. It runs at the same time as the server
packages and does not change `SHA256SUMS`.

Setup:

1. On the build machine, install Unreal Engine 5.4+ with the toolchain for
   its platform: Visual Studio 2022 with the C++ game workload on Windows,
   Xcode on macOS, or Epic's clang toolchain on Linux. Also install `zip`
   on Linux/macOS.
2. Register it as a self-hosted runner for this repository
   (*Settings → Actions → Runners → New self-hosted runner*). Give it the
   extra label `unreal`. Run it as a service with enough disk space: an
   engine build plus cooked content needs tens of GB.
3. Repository variables (*Settings → Secrets and variables → Actions →
   Variables*):
   * `TFPS_UNREAL_RUNNER` = `true`. Without it the job is skipped, so releases
     work normally without such a runner.
   * `UE_ROOT` = engine folder (the one that contains `Engine/`), e.g.
     `C:\Program Files\Epic Games\UE_5.4`. This is optional if the runner's
     service environment already sets `UE_ROOT`. It is the same for every
     runner, so with machines on different OSes, leave it unset and set
     `UE_ROOT` in each runner's environment (the runner's `.env` file).
   * `TFPS_UNREAL_PLATFORMS` = JSON list of client platforms, e.g.
     `["Win64"]` (the default) or `["Win64","Linux","Mac"]`. Every platform
     listed needs an online runner of that OS labelled `unreal`. Otherwise
     its job waits in the queue until it times out.
4. Publish a release, or re-run the workflow with the tag.
