# Platform support

Targets: Windows (x64), Linux (x64), macOS (Apple Silicon and Intel).

## Rust server

Plain Rust (std, Tokio, no platform-specific code beyond `SIGTERM` handling
behind `cfg(unix)`). CI builds and tests on Ubuntu, Windows and macOS.

## C++ NetCore (client netcode + movement)

Standard C++17 with no dependencies. CI builds it and runs the conformance
tests with:

| Platform | Compiler | Float modes tested |
|----------|----------|--------------------|
| Linux | GCC | default, `-ffp-contract=fast -march=native` |
| Windows | MSVC | `/fp:precise`, `/fp:fast /arch:AVX2` |
| macOS (ARM64) | Apple Clang | default (FMA contraction on), `-march=native` |

Bit-exact results with the Rust server are required in every mode. The
per-file pragmas in `TfpsFloatDeterminism.h` keep them exact even when the
project is compiled with fast-math options.

## Game client (Godot 4.7)

| | Windows | Linux | macOS |
|-|---------|-------|-------|
| Renderer | Forward+ (Vulkan / Direct3D 12) | Forward+ (Vulkan) | Forward+ (Metal via MoltenVK) |
| Build | `windows-latest`, MSVC extension | `ubuntu-latest`, GCC extension | `macos-latest`, universal (arm64 + x86_64) extension |
| Verified | built and packaged in CI | built, exported game joins a practice match in CI; rendered here in software | built and packaged in CI |

Graphics presets (Low / Medium / High / Ultra) scale SSAO, SSIL,
volumetric fog and SDFGI for weaker GPUs. None of this has been measured
on real GPUs yet.

## Packaging

* Win64 client: build on Windows (`Scripts\package_client.ps1`).
* Linux client: build on Linux (`Scripts/package_client.sh Linux`), or
  cross-compile from Windows with Epic's Linux toolchain.
* Mac client: build on macOS (`Scripts/package_client.sh Mac`). Needs Xcode.
* Server: `Scripts/package_server.sh [target-triple]` on the target OS, or
  cross-compile with the matching Rust target.
* Release archives (server, lobby, bot, C++ CLI client) for all three OSes:
  built by `.github/workflows/release.yml` when a release is published, or
  locally with `Scripts/package_release.sh <tag>`. See [RELEASES.md](RELEASES.md).
