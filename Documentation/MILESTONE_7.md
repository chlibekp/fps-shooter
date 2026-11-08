# Phase 7 report: polish

Goal (roadmap): polish on top of Phases 1–6. That means robustness under
load, real menus instead of console commands, audio, visual effects, and
release packaging.

**Result:** the engine-independent parts all work and are tested natively:
* the snapshot byte budget;
* the menu logic;
* the sound synthesiser;
* the particle system;
* the release packaging scripts.

The Unreal code that draws the menus and particles and plays the sounds is
written and mock-compiled. As in every phase, it has not been run in Unreal
Engine. The release workflow now has a free route to a playable Linux
build, but that has not run yet either (it needs an Epic token; see below).

## Snapshot byte budget (`Shared/Protocol/rust/src/snapshot.rs`)

* **The bug:** a full 64-player server could build snapshots over the
  1200-byte packet limit.
* **The budget:** `encode_budgeted` keeps every snapshot inside the limit.
  * It never drops a known player. At worst a player is sent "unchanged"
    (4 bytes).
  * Changes go nearest-player-first.
  * A held-back change is sent in a later update: the server remembers what
    the client actually has, not the true state.
  * Leftover space carries gadgets, then shot events.
  * The wire format is unchanged.
* **Fast path:** if the whole snapshot fits (the usual case) it is written
  directly. Without this, encoding cost 2.2× more (see Performance).
* **Tests:**
  * two protocol unit tests;
  * a 64-player end-to-end test (`Server/tests/full_server.rs`). All 64
    move, trimming happens, then every client converges on all 63 others
    (within 5 cm).

## Menus and settings (`Ui/TfpsMenu.*`; `ATfpsPlayerController`, `ATfpsHUD`)

* **Screens:**
  * main menu (host a lobby, join by invite code, direct connect, name);
  * lobby (squad, ready; the host edits map, mode, teams, rounds, timers,
    friendly fire and tick rate, and starts the match);
  * pause menu;
  * settings (sensitivity, aiming sensitivity, invert Y, crouch
    toggle/hold, field of view, volumes, network stats, lobby service);
  * controls (rebind any action; a clash swaps keys; reset to defaults).
* **Input:** keyboard and mouse navigation and text entry.
* **Saving:** a locale-independent settings file with validation
  (`Saved/TfpsUser.cfg`).
* **Behaviour:** the click that closes the menu does not fire. The lobby
  API now lists the maps it can host.
* **Tests:** 8 native unit tests. The Unreal drawing and input are
  mock-checked only.

## Audio (`Audio/TfpsSynth.*`, `UTfpsAudioSubsystem`): see AUDIO.md

* **Sounds:** 38 cues with 98 variations, all synthesised with no
  recordings. They cover weapons, reloads, footsteps by floor material,
  impacts by surface, breaking, explosions, flashbang ringing, smoke, the
  drone motor loop, feedback and menu sounds.
* **Tests:** 5 native tests check levels, clean endings, variations, the
  seamless loop and frequency content. `tfps_render_sounds` writes every
  sound as WAV.
* **Tuning:** the recipes were tuned from waveform and spectrogram plots:
  * the explosion was a synth sine; it is now noise-driven;
  * glass and metal rang too long.
* **Unreal playback:** sounds play through `USoundWaveProcedural` with
  per-cue distance falloff and rate limits. It is hooked into every source
  above. Mock-checked only; nobody has heard it in the engine.

## Visual effects (`Fx/TfpsParticles.*`, `ATfpsParticleFx`): see VFX.md

* **Effects:**
  * impact bursts by surface;
  * muzzle smoke;
  * explosions;
  * flash bursts;
  * chunks and dust from broken cells, which bounce off the map and come
    to rest;
  * smoke-grenade clouds.
* **Smoke clouds:** clouds keep evenly spread slots filled with
  overlapping puffs of staggered lifetimes. Tests require ≥ 95 % of the
  dome covered at 4, 8 and 12 s; 100 % was measured on every seed.
  Randomly placed puffs left 4–15 % gaps, and puffs refreshed only after
  dying left up to 28 %.
* **Tests:** 5 native tests.
* **Unreal:** one instanced mesh per look, 🔒 tinted engine shapes. This
  replaces the grey smoke sphere and the physics-cube debris pool.

## Releases (`.github/workflows/release.yml`, `unreal-linux.yml`): see RELEASES.md

* **Server tools (verified on Linux):** each release gets server, lobby,
  bot and command-line client archives for Linux, Windows and macOS.
  * They are smoke-tested from the archive itself: lobby health check, and
    both headless clients connect.
  * Built by a background agent during this phase. Only the Linux packaging
    and smoke test have been run, here; the Windows and macOS steps run for
    the first time on the first release.
* **Playable download:** `TacticalFPS-<tag>-<os>-<arch>` holds the game,
  server, lobby, bots and one-click launchers:
  * `Practice` starts a local match against moving target bots;
  * `Play` opens the menu with a local lobby service;
  * `Play join HOST` uses a friend's lobby.
* **What was verified locally:** packaging, plus a test that runs Practice
  exactly as a player would. It used a stand-in client (the C++ client,
  same netcode). The game joined with 2 bots, and nothing was left running
  afterwards.
* **Building the Unreal client:**
  * **Linux, free:** a GitHub runner uses Epic's Unreal container after
    freeing disk. It is optionally faster on Blacksmith runners
    (`TFPS_UE_RUNNER`). It needs the `EPIC_GHCR_TOKEN` secret, has never
    run, and expects first-compile errors.
  * **Windows / macOS:** only on a machine with Unreal (self-hosted
    runner). Unreal cannot build them on Linux.

## Tests

| Suite | Result |
|-------|--------|
| Rust: protocol, simulation, server unit + e2e (incl. the 64-player test), lobby, map validation | 123 pass |
| C++ NetCore offline: protocol / golden / conformance, menu (8), audio (5), particles (5) | 28 pass |
| C++ NetCore live against the Rust server | 3 pass |
| Unreal glue mock compile check (all files) | pass |
| Sound render (98 WAV files) | pass |
| Release packaging and smoke test (Linux) | pass |
| Game download smoke test (Linux, stand-in client) | pass |
| actionlint + shellcheck on the workflows and scripts | pass |

`Scripts/test_all.sh` runs everything above except the release and
download checks, which use their own scripts (Commands). CI was green
through the particle-effects commit at the time of writing.

## Performance (criterion, this container, no network)

| Measurement | Result |
|-------------|--------|
| Snapshot encoding for all clients, 2 / 10 / 32 players | 0.15 µs / 3.2 µs / 27.5 µs |
| … before the budget change, same machine and run | 27.0 µs (32 players) |
| … with the first budget version (no fast path) | 39.6 µs (fixed) |
| World tick, 32 players moving and firing | 38 µs (0.5 % of a 128 Hz tick) |
| World tick, 32 players throwing grenades | 16 µs |
| Sound synthesis | the whole audio test binary (rendering every cue several times) runs in 0.5 s; the game renders each cue once and caches it |

This container is shared and noisy. Compare numbers only within one run, as
the "before / after" rows do.

## Known issues and limitations

* None of the Unreal code (menus, audio playback, particles, HUD, lobby
  client) has run in the engine. The first real compile will need fixes.
  The free Linux CI build is the planned way to get there.
* Menus and particles are Canvas-drawn and tinted engine shapes 🔒. There
  are no UMG widgets, Niagara systems or authored textures.
* Sound has no occlusion or room reverb, and there is no music.
* Particles and footstep surfaces use the static map, not destroyed or
  added cells.
* Practice bots move but do not shoot.
* Windows and macOS game downloads need a self-hosted Unreal machine.
  macOS builds are unsigned.
* The Windows launchers (`Play.ps1` / `.bat`) and the Windows release steps
  have not run.

## Commands

```bash
Scripts/test_all.sh
```

```bash
cargo test -p tfps-server --release --test full_server
```

```bash
build/netcore/tfps_netcore_tests Menu
```

```bash
build/netcore/tfps_netcore_tests Audio
```

```bash
build/netcore/tfps_netcore_tests Fx
```

```bash
build/netcore/tfps_render_sounds build/sounds
```

```bash
cargo bench -p tfps-server --bench tick
```

```bash
Scripts/package_release.sh v0.0.0-local Dist/release
```

```bash
Scripts/package_game.sh v0.0.0-local Linux Dist/client-Linux Dist/game
```

```bash
Scripts/smoke_test_game.sh Dist/game/TacticalFPS-v0.0.0-local-linux-x86_64.tar.gz
```
