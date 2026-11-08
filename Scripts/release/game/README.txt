TacticalFPS @TAG@ (@PLATFORM@)
==============================

Play
----
  Practice:  double-click Practice.sh (Practice.bat on Windows)
             straight into a deathmatch against moving target bots.
             The bots move but do not shoot back yet.
  Play:      double-click Play.sh (Play.bat on Windows)
             main menu. Host a lobby and give friends the invite code, or
             join one. A lobby service starts on this computer for hosting.

From a terminal:
  ./Play.sh practice harbor 6     map (test_arena, harbor, villa, consulate) and bot count
  ./Play.sh join 192.168.1.20     use a friend's lobby (the computer running Play)
  Windows: Play.bat practice harbor 6 / Play.bat join 192.168.1.20

Playing with friends
--------------------
The host runs Play and hosts a lobby from the menu. Friends run
"Play join <host address>" and enter the invite code. On a LAN this works
as is. Over the internet the host must forward TCP port 7700 (lobby) and
the match's UDP ports to their computer, and start with
TFPS_PUBLIC_HOST=<public address> set, so friends are sent there.

Controls
--------
WASD move, mouse look, Shift sprint, Alt walk, C crouch (Ctrl hold),
Z prone, Space jump, Q/E lean, right mouse aim, left mouse fire, R reload,
F use (plant, defuse, reinforce), 1/2 weapons, 3-6 gadgets, F3 network
stats, Esc menu. Everything is rebindable in Settings > Controls.

Folders
-------
  Game/    the game client
  Server/  game server, lobby service and bots used by Play / Practice
  Logs/    server, lobby and bot logs (created on first run)

Version: see VERSION.txt
