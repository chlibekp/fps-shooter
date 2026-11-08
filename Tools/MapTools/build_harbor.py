#!/usr/bin/env python3
"""Generates Shared/Definitions/maps/harbor.json — "Harbor Warehouse".

A dockside warehouse: a 36 x 24 m hall (brick to 3.5 m, metal above), a
two-storey office block in the north-east corner, a mezzanine along the
south wall, loading doors on the west and north sides and a container yard
around it. Defenders start in the offices; attackers in the west yard.

Objective sites: "Upper Office" and "Storage" (ground-floor office room).
Run: python3 Tools/MapTools/build_harbor.py
"""
from mapkit import Map, door, window

m = Map("harbor")

# --- Ground, yard perimeter --------------------------------------------------
m.box(-42, -34, -0.5, 42, 34, 0.0, "dirt", "ground")
PH = 3.5
m.box(-42, 33.6, 0, 42, 34, PH, "concrete", "perimeter")
m.box(-42, -34, 0, 42, -33.6, PH, "concrete", "perimeter")
m.box(41.6, -33.6, 0, 42, 33.6, PH, "concrete", "perimeter")
m.box(-42, -33.6, 0, -41.6, 33.6, PH, "concrete", "perimeter")

# --- Warehouse shell: x -10..26, y -12..12 -------------------------------------
X0, X1, Y0, Y1 = -10.0, 26.0, -12.0, 12.0
T = 0.3
LOW, TOP = 3.5, 8.0
m.box(X0, Y0, 0, X1, Y1, 0.02, "concrete", "floor")
# West wall: open loading door (4 m) and a man door.
m.wall("y", X0, X0 + T, Y0, Y1, 0.02, LOW, "brick", "wall_w",
       [door(-6.0, 4.0, 3.4, fill=None), door(6.0)])
# South wall: a door and two windows.
m.wall("x", Y0, Y0 + T, X0 + T, X1 - T, 0.02, LOW, "brick", "wall_s",
       [window(-1.0, 1.6), door(6.0), window(16.0, 1.6)])
# North wall: barricaded loading dock (4 m) and a window into the offices.
m.wall("x", Y1 - T, Y1, X0 + T, X1 - T, 0.02, LOW, "brick", "wall_n",
       [door(4.0, 4.0, 3.4, fill="barricade"), window(20.0, 1.6)])
# East wall: door into the ground-floor storage room.
m.wall("y", X1 - T, X1, Y0, Y1, 0.02, LOW, "brick", "wall_e", [door(7.0)])
# Upper walls (metal siding, indestructible) and roof.
m.box(X0, Y0, LOW, X1, Y0 + T, TOP, "metal", "siding")
m.box(X0, Y1 - T, LOW, X1, Y1, TOP, "metal", "siding")
m.box(X0, Y0 + T, LOW, X0 + T, Y1 - T, TOP, "metal", "siding")
m.box(X1 - T, Y0 + T, LOW, X1, Y1 - T, TOP, "metal", "siding")
m.box(X0, Y0, TOP, X1, Y1, TOP + 0.3, "concrete", "roof")

# --- Office block: x 14..26, y 2..12, two floors ------------------------------
OX0, OY0 = 14.0, 2.0
G = 0.02
F1, SLAB, F2, OROOF = 3.2, 3.5, 6.5, 6.7
DT = 0.12  # drywall thickness
# Ground floor: south wall (to the hall) with a door and a window, west wall
# with a door; partition at x = 20 between Dispatch and Storage.
m.wall("x", OY0, OY0 + DT, OX0, X1 - T, G, F1, "drywall", "office_s1",
       [door(16.5, fill=None), window(23.0, 1.6)])
m.wall("y", OX0, OX0 + DT, OY0 + DT, Y1 - T, G, F1, "drywall", "office_w1",
       [door(8.0), window(4.5, 1.6)])
m.wall("y", 20.0 - DT / 2, 20.0 + DT / 2, OY0 + DT, Y1 - T, G, F1, "drywall", "office_mid1",
       [door(5.0, fill=None)])
# Upper floor slab with a breakable hatch over Storage.
m.floor(OX0, OY0, X1 - T, Y1 - T, SLAB, SLAB - F1, "concrete", "slab",
        holes=[(22.0, 8.0, 23.4, 9.4, "wood")])
# Upper floor walls: door from the catwalk, windows over the hall.
m.wall("x", OY0, OY0 + DT, OX0, X1 - T, SLAB, F2, "drywall", "office_s2",
       [door(15.6, fill=None), window(19.0, 2.0), window(23.5, 1.6)])
m.wall("y", OX0, OX0 + DT, OY0 + DT, Y1 - T, SLAB, F2, "drywall", "office_w2",
       [window(7.0, 2.0)])
m.box(OX0, OY0, F2, X1 - T, Y1 - T, OROOF, "concrete", "office_roof")
# Office furniture (cover): desks and filing cabinets.
for (x, y) in [(16.0, 6.0), (18.0, 9.5), (22.5, 4.5)]:
    m.box(x - 0.8, y - 0.4, G, x + 0.8, y + 0.4, 0.75, "wood", "desk")
for (x, y) in [(17.0, 5.0), (21.0, 10.0), (24.5, 6.0)]:
    m.box(x - 0.8, y - 0.4, SLAB, x + 0.8, y + 0.4, SLAB + 0.75, "wood", "desk")
m.box(25.0, 9.0, G, 25.6, 11.6, 1.9, "metal", "shelf")
m.box(20.3, 11.0, G, 23.0, 11.6, 1.9, "metal", "shelf")

# Stairs from the hall up to a catwalk along the office's south wall.
m.stairs(7.5, 0.4, 12.5, 1.9, G, SLAB, "+x", "concrete")
m.box(12.5, 0.4, SLAB - 0.2, 17.0, OY0, SLAB, "metal", "catwalk")
m.box(12.5, 0.32, SLAB, 17.0, 0.4, SLAB + 1.0, "metal", "railing")

# --- Mezzanine along the south wall ------------------------------------------
MZ = 4.0
m.box(-6.0, Y0 + T, MZ - 0.2, 10.0, -9.0, MZ, "metal", "mezzanine")
m.box(-3.5, -9.08, MZ, 10.0, -9.0, MZ + 1.0, "metal", "railing")
m.stairs(-9.5, -9.0, -3.5, -7.6, G, MZ, "+x", "concrete")
# Crates on the mezzanine.
m.crate(2.0, -10.6, 1.2, MZ)
m.crate(7.0, -10.8, 1.0, MZ)

# --- Hall cover ----------------------------------------------------------------
m.container(-4.0, 3.0, True)
m.container(-4.0, 5.6, True, open_end=False)
m.container(4.0, -6.0, False)
for (x, y, s) in [(0.0, -2.0, 1.2), (1.3, -2.1, 1.2), (8.0, 8.0, 1.2), (8.0, 9.3, 1.2), (-8.0, -4.0, 1.0),
                  (12.0, -8.0, 1.2), (18.0, -6.0, 1.2), (22.0, -9.0, 1.2), (22.0, -2.0, 1.0)]:
    m.crate(x, y, s)
m.crate(8.0, 8.0, 1.2, 1.2)
# Forklift-height pallet racks (metal, indestructible) in the hall.
for x in [14.0, 19.0]:
    m.box(x, -9.0, G, x + 3.6, -8.2, 3.0, "metal", "rack")

# --- Yard -----------------------------------------------------------------------
for (x, y, ax) in [(-30.0, -20.0, True), (-30.0, -17.4, True), (-22.0, 14.0, False), (-18.0, 18.0, True),
                   (30.0, -24.0, False), (30.0, 18.0, True), (6.0, 20.0, True), (6.0, -22.0, True)]:
    m.container(x, y, ax)
m.container(-30.0, -20.0, True, z=2.6, open_end=False)  # stacked
# Jersey barriers and a guard booth near the attackers' approach.
for (cx, cy, along_x) in [(-20.0, -4.0, False), (-20.0, 6.0, False), (-14.0, 22.0, True), (14.0, -18.0, True)]:
    if along_x:
        m.box(cx - 1.5, cy - 0.3, 0, cx + 1.5, cy + 0.3, 0.9, "concrete", "barrier")
    else:
        m.box(cx - 0.3, cy - 1.5, 0, cx + 0.3, cy + 1.5, 0.9, "concrete", "barrier")
m.wall("x", -28.0, -27.85, -34.0, -31.0, 0, 2.6, "wood", "booth", [window(-32.5, 1.2)])
m.wall("x", -25.15, -25.0, -34.0, -31.0, 0, 2.6, "wood", "booth", [door(-32.5)])
m.box(-34.0, -28.0, 0, -33.85, -25.0, 2.6, "wood", "booth")
m.box(-31.15, -28.0, 0, -31.0, -25.0, 2.6, "wood", "booth")
m.box(-34.0, -28.0, 2.6, -31.0, -25.0, 2.75, "wood", "booth_roof")
# Bollards along the quay (north edge).
for x in range(-36, 40, 6):
    m.box(x - 0.2, 30.0, 0, x + 0.2, 30.4, 1.0, "metal", "bollard")

# --- Spawns and objectives ---------------------------------------------------------
for (x, y) in [(-36.0, 0.0), (-36.0, 4.0), (-36.0, -4.0), (-34.0, 8.0), (-34.0, -8.0)]:
    m.spawn(x, y, 0.0, 0.0, 0)
for (x, y, z) in [(17.0, 8.0, G), (23.0, 6.0, G), (15.5, 3.2, SLAB), (23.2, 10.8, SLAB), (24.0, 4.0, SLAB)]:
    m.spawn(x, y, z, 180.0, 1)
m.objective("Upper Office", 14.2, 2.2, SLAB, 25.6, 11.6, F2)
m.objective("Storage", 20.1, 2.2, G, 25.6, 11.6, F1)
m.write()
