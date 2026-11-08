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
| Windows | MSVC | `/fp:precise`, `/fp:fast /arch:AVX2` (Unreal uses `/fp:fast`) |
| macOS (ARM64) | Apple Clang | default (FMA contraction on), `-march=native` |

Bit-exact results with the Rust server are required in every mode. The
per-file pragmas in `TfpsFloatDeterminism.h` keep them exact even when the
project is compiled with fast-math options.

## Unreal Engine features

The engine version is 5.4 (`EngineAssociation`). The scripts also accept any
5.4+ install via `UE_ROOT`.

| Feature | Windows (DX12 SM6) | Linux (Vulkan SM6) | macOS (Metal) | Fallback |
|---------|-------------------|--------------------|---------------|----------|
| Lumen GI & reflections | Yes | Yes | Yes on Apple Silicon (UE 5.x) | Screen-space GI / SSR via scalability "Low" |
| Nanite | Yes | Yes | Apple Silicon M2+ only (UE 5.4+); otherwise no | LOD1 meshes from the asset pipeline |
| Virtual shadow maps | Yes | Yes | Yes (Apple Silicon) | Shadow maps |
| Chaos physics / destruction (Phase 3) | Yes | Yes | Yes | n/a (gameplay destruction is server-driven) |
| Hardware ray tracing | Off by default | Off | Not supported | Lumen software tracing |

Ray tracing is disabled project-wide so behaviour is the same on all three
platforms. The availability notes above come from Epic's documentation as we
understand it. They must be confirmed on real hardware in Phase 7
cross-platform testing, which is not done yet.

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
