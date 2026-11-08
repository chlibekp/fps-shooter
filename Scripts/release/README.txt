TacticalFPS @TAG@ - server tools for @PLATFORM@
===================================================

This archive holds the parts of TacticalFPS that run without Unreal Engine:

  tfps-server        authoritative dedicated game server (UDP)
  tfps-lobby         lobby service: invite codes, custom matches (HTTP);
                     it starts its matches in-process, no tfps-server needed
  tfps-bot           headless Rust bot client(s) for testing a server
  tfps_cli_client    headless C++ client (the Unreal client's netcode)
  tfps_render_sounds writes every procedural game sound as WAV files:
                     tfps_render_sounds <output folder>
  Shared/Definitions maps, weapons and gadgets; the server, lobby and
                     clients read these at runtime - keep them next to
                     the binaries
  run_*              start scripts (.sh on Linux/macOS, .ps1 on Windows)

The playable Unreal Engine game client is NOT in this archive. It needs
Unreal Engine to build; see Documentation/RELEASES.md in the repository.

Quick start
-----------
Linux / macOS (terminal, in this folder):

  ./run_server.sh --bind 0.0.0.0:7777 --map harbor --mode defuse
  ./run_lobby.sh  --bind 0.0.0.0:7700 --public-host <your public IP or host>
  ./run_bot.sh    --server 127.0.0.1:7777 --count 4 --seconds 60
  ./run_cli_client.sh --server 127.0.0.1:7777 --seconds 10

Windows (PowerShell, in this folder):

  .\run_server.ps1 --bind 0.0.0.0:7777 --map harbor --mode defuse
  .\run_lobby.ps1  --bind 0.0.0.0:7700 --public-host <your public IP or host>
  .\run_bot.ps1    --server 127.0.0.1:7777 --count 4 --seconds 60
  .\run_cli_client.ps1 --server 127.0.0.1:7777 --seconds 10

  If PowerShell refuses to run scripts:
    powershell -ExecutionPolicy Bypass -File .\run_server.ps1

Every program prints its options with --help. Maps: test_arena, harbor,
villa, consulate. Modes: deathmatch, elimination, defuse.

Ports: the server uses UDP 7777 by default, the lobby TCP 7700 plus one UDP
port per running match (any free port). Open them in your firewall to host
for other machines.

The binaries are not code-signed:
  macOS:   xattr -dr com.apple.quarantine .   (once, in this folder)
  Windows: SmartScreen may warn; choose "More info" -> "Run anyway".

Lobby health check: curl http://127.0.0.1:7700/health

Integrity: compare the archive against SHA256SUMS on the release page.
