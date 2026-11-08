#!/usr/bin/env python3
"""Generates Shared/Definitions/maps/consulate.json — "Consulate".

A two-storey office building (30 x 22 m) around a double-height atrium,
behind a walled courtyard with a gate, a fountain and planters, and a
service alley on the east side. Exterior walls are brick panels between
concrete columns; interior walls are drywall.

Ground floor: lobby (south) with reception and two stairs, archives (west
wing), security room (east wing). Upper floor: galleries around the
atrium (glass balustrades), conference room (west), offices (east) and the
ambassador's office (north of the atrium).

Objective sites: "Archives" (ground floor) and "Ambassador Office" (upper).
Run: python3 Tools/MapTools/build_consulate.py
"""
from mapkit import Map, door, window

m = Map("consulate")

X0, X1, Y0, Y1 = -15.0, 15.0, -11.0, 11.0
T, DT = 0.35, 0.12
G = 0.1
F1, SLAB = 3.6, 3.9
F2, ROOF = 7.2, 7.5
AX, AY = 4.0, 4.0   # atrium: x -4..4, y -4..4

# --- Ground, plot walls, street ------------------------------------------------------
m.box(-34, -46, -0.5, 34, 26, 0.0, "dirt", "ground")
PW = 3.0
m.wall("x", -30.3, -30.0, -28.0, 28.0, 0, PW, "concrete", "plot_wall",
       [door(-12.0, 3.0, 2.6, fill=None), door(12.0, 3.0, 2.6, fill=None)])
m.box(-28.3, -30.3, 0, -28.0, 20.0, PW, "concrete", "plot_wall")
m.box(28.0, -30.3, 0, 28.3, 20.0, PW, "concrete", "plot_wall")
m.box(-28.3, 20.0, 0, 28.3, 20.3, PW, "concrete", "plot_wall")
# Street boundary (south) and parked vehicles.
m.box(-34, -46, 0, 34, -45.7, 4.0, "concrete", "street_wall")
m.box(-34.0, -45.7, 0, -33.7, -30.3, 4.0, "concrete", "street_wall")
m.box(33.7, -45.7, 0, 34.0, -30.3, 4.0, "concrete", "street_wall")
for (x, y) in [(-20.0, -38.0), (2.0, -40.0), (22.0, -37.0)]:
    m.box(x - 2.3, y - 0.9, 0.25, x + 2.3, y + 0.9, 1.0, "metal", "car")
    m.box(x - 1.1, y - 0.8, 1.0, x + 1.2, y + 0.8, 1.45, "metal", "car_cabin")

# --- Courtyard ------------------------------------------------------------------------
m.box(-2.5, -22.5, 0, 2.5, -17.5, 0.8, "concrete", "fountain")
for (x, y) in [(-14.0, -18.0), (14.0, -18.0), (-8.0, -25.0), (8.0, -25.0), (-20.0, -14.0), (20.0, -14.0)]:
    m.box(x - 1.2, y - 0.6, 0, x + 1.2, y + 0.6, 0.9, "concrete", "planter")
# Gatehouse by the west gate.
m.wall("x", -28.0, -27.85, -27.85, -22.0, 0, 2.6, "brick", "gatehouse", [window(-25.0, 1.4)])
m.wall("y", -22.15, -22.0, -30.0, -28.0, 0, 2.6, "brick", "gatehouse", [door(-29.0, 0.9, fill=None)])
m.box(-28.0, -30.0, 2.6, -22.0, -27.85, 2.75, "concrete", "gatehouse_roof")
# Service alley (east): dumpsters and a container.
m.box(17.0, -6.0, 0, 19.0, -4.5, 1.4, "metal", "dumpster")
m.box(17.0, 2.0, 0, 19.0, 3.5, 1.4, "metal", "dumpster")
m.container(19.5, 6.0, False)

# --- Building shell: concrete columns + brick panels --------------------------------
m.floor(X0, Y0, X1, Y1, G, 0.3, "concrete", "floor")
for z0, z1, sfx in [(G, F1, "1"), (SLAB, F2, "2")]:
    up = sfx == "2"
    m.wall("x", Y0, Y0 + T, X0, X1, z0, z1, "brick", "wall_s" + sfx,
           [window(-11.0, 1.6), window(-7.0, 1.6), window(7.0, 1.6), window(11.0, 1.6)] +
           ([window(-2.0, 1.6), window(2.0, 1.6)] if up else [door(0.0, 2.4, 2.6)]))
    m.wall("x", Y1 - T, Y1, X0, X1, z0, z1, "brick", "wall_n" + sfx,
           [window(-10.0, 1.6), window(-6.0, 1.6), window(0.0, 2.0), window(6.0, 1.6)] +
           ([window(10.0, 1.6)] if up else [door(10.0)]))
    m.wall("y", X0, X0 + T, Y0 + T, Y1 - T, z0, z1, "brick", "wall_w" + sfx,
           [window(-6.0, 1.6), window(6.0, 1.6)] + ([window(0.0, 1.6)] if up else [door(0.0)]))
    m.wall("y", X1 - T, X1, Y0 + T, Y1 - T, z0, z1, "brick", "wall_e" + sfx,
           [window(-6.0, 1.6), window(6.0, 1.6)] + ([window(0.0, 1.6)] if up else [door(-1.0)]))
for (cx, cy) in [(X0, Y0), (X1, Y0), (X0, Y1), (X1, Y1), (-5.0, Y0), (5.0, Y0), (-5.0, Y1), (5.0, Y1)]:
    m.box(cx - 0.3, cy - 0.3, 0, cx + 0.3, cy + 0.3, ROOF, "concrete", "column")
m.box(X0, Y0, F2, X1, Y1, ROOF, "concrete", "roof")

# --- Ground floor interior ----------------------------------------------------------------
# Wings: walls along x = -4 and x = 4 (atrium flanks), and y = -4 (lobby | wings).
m.wall("y", -AX - DT / 2, -AX + DT / 2, -AY, Y1 - T, G, F1, "drywall", "wing_wall1",
       [door(0.0, fill=None), window(7.0, 2.0)])
m.wall("y", AX - DT / 2, AX + DT / 2, -AY, Y1 - T, G, F1, "drywall", "wing_wall1",
       [door(0.0, fill=None), window(7.0, 2.0)])
m.wall("x", -AY - DT / 2, -AY + DT / 2, X0 + T, -AX, G, F1, "drywall", "lobby_wall1",
       [door(-7.0, fill=None), window(-11.0, 2.0)])
m.wall("x", -AY - DT / 2, -AY + DT / 2, AX, X1 - T, G, F1, "drywall", "lobby_wall1",
       [door(7.0, fill="door"), window(11.0, 2.0)])
# Atrium north wall (below the ambassador's office) with a doorway to the back corridor.
m.wall("x", AY - DT / 2, AY + DT / 2, -AX + DT / 2, AX - DT / 2, G, F1, "drywall", "atrium_n1",
       [door(0.0, 1.6, fill=None)])
# Reception desk, archive shelves, security room.
m.box(-2.5, -8.5, G, 2.5, -7.8, G + 1.1, "wood", "reception")
for y in [-2.0, 1.0, 4.0, 7.0]:
    m.box(-13.5, y, G, -6.5, y + 0.6, G + 2.1, "metal", "archive_shelf")
m.box(10.0, 4.0, G, 13.0, 5.0, G + 0.8, "wood", "desk")
m.box(14.0, -3.0, G, 14.6, 3.0, G + 2.0, "metal", "server_rack")
m.box(-1.5, -1.5, G, 1.5, 1.5, G + 0.6, "concrete", "atrium_planter")

# Stairs in the lobby: east rises toward +x, west toward -x, both to SLAB.
m.stairs(8.6, -10.6, 14.6, -9.0, G, SLAB, "+x")
m.stairs(-14.6, -10.6, -8.6, -9.0, G, SLAB, "-x")

# --- Upper floor -------------------------------------------------------------------------
m.floor(X0, Y0, X1, Y1, SLAB, SLAB - F1, "concrete", "slab",
        holes=[(-AX, -AY, AX, AY, None), (8.6, -10.6, 14.6, -9.0, None), (-14.6, -10.6, -8.6, -9.0, None)])
# Glass balustrades around the atrium (south, west, east edges) and the stair holes.
m.box(-AX, -AY - 0.04, SLAB, AX, -AY, SLAB + 1.1, "glass", "balustrade")
m.box(-AX - 0.04, -AY, SLAB, -AX, AY, SLAB + 1.1, "glass", "balustrade")
m.box(AX, -AY, SLAB, AX + 0.04, AY, SLAB + 1.1, "glass", "balustrade")
m.box(8.6, -9.0, SLAB, 13.6, -8.96, SLAB + 1.0, "metal", "railing")
m.box(-13.6, -9.0, SLAB, -8.6, -8.96, SLAB + 1.0, "metal", "railing")
# Ambassador's office: north of the atrium, between x -4 and 4.
m.wall("x", AY - DT / 2, AY + DT / 2, -AX, AX, SLAB, F2, "drywall", "ambassador_s",
       [window(-2.0, 2.0), door(2.2, fill="door")])
m.wall("y", -AX - DT / 2, -AX + DT / 2, AY + DT / 2, Y1 - T, SLAB, F2, "drywall", "ambassador_w",
       [door(8.0, fill=None)])
m.wall("y", AX - DT / 2, AX + DT / 2, AY + DT / 2, Y1 - T, SLAB, F2, "drywall", "ambassador_e")
m.box(-1.5, 7.5, SLAB, 1.5, 8.5, SLAB + 0.8, "wood", "desk")
m.box(-3.5, 10.0, SLAB, 3.5, 10.6, SLAB + 2.0, "wood", "bookcase")
# Conference room (west) and offices (east), separated from the galleries.
m.wall("y", -8.0 - DT / 2, -8.0 + DT / 2, -7.0, Y1 - T, SLAB, F2, "drywall", "conference_e",
       [door(-2.0, fill=None), window(3.0, 2.0)])
m.box(-13.0, -1.0, SLAB, -10.0, 4.0, SLAB + 0.75, "wood", "conference_table")
m.wall("y", 8.0 - DT / 2, 8.0 + DT / 2, -7.0, Y1 - T, SLAB, F2, "drywall", "offices_w",
       [door(-2.0, fill=None), door(6.0, fill=None)])
m.wall("x", 3.0 - DT / 2, 3.0 + DT / 2, 8.0 + DT / 2, X1 - T, SLAB, F2, "drywall", "offices_mid",
       [door(11.5, fill=None)])
m.box(10.0, 6.0, SLAB, 12.0, 7.0, SLAB + 0.75, "wood", "desk")
m.box(10.0, -3.0, SLAB, 12.0, -2.0, SLAB + 0.75, "wood", "desk")

# --- Spawns and objectives ------------------------------------------------------------------
for (x, y) in [(-16.0, -38.0), (-8.0, -36.0), (0.0, -35.0), (8.0, -36.0), (16.0, -38.0)]:
    m.spawn(x, y, 0.0, 90.0, 0)
for (x, y, z) in [(-10.0, 9.5, G), (-6.0, -3.0, G), (11.0, 8.0, G), (0.0, 6.0, SLAB), (-2.0, 9.0, SLAB)]:
    m.spawn(x, y, z, 270.0, 1)
m.objective("Archives", X0 + T, -AY + DT, G, -AX - DT, Y1 - T, F1)
m.objective("Ambassador Office", -AX + DT, AY + DT, SLAB, AX - DT, Y1 - T, F2)
m.write()
