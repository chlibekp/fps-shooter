#!/usr/bin/env python3
"""Generates Shared/Definitions/maps/test_arena.json.

The test arena is the Phase 1 graybox: a fenced yard with a two-storey
building (doors, windows, interior drywall, a staircase), cover objects and
a shipping container. All gameplay geometry is axis-aligned boxes in meters
(Z-up, X forward). Run: python3 Tools/MapTools/build_test_arena.py
"""
import json
import os

solids = []


def box(x0, y0, z0, x1, y1, z1, material, tag):
    lo = [min(x0, x1), min(y0, y1), min(z0, z1)]
    hi = [max(x0, x1), max(y0, y1), max(z0, z1)]
    solids.append({"min": [round(v, 4) for v in lo], "max": [round(v, 4) for v in hi],
                   "material": material, "tag": tag})


def wall_x(y0, y1, x_from, x_to, z0, z1, material, tag, openings=()):
    """Wall running along X between x_from..x_to with thickness y0..y1.
    openings: (xa, xb, sill_z, head_z) holes cut out of the wall."""
    cuts = sorted(openings)
    x = x_from
    for (xa, xb, sill, head) in cuts:
        if xa > x:
            box(x, y0, z0, xa, y1, z1, material, tag)
        if sill > z0:
            box(xa, y0, z0, xb, y1, sill, material, tag + "_sill")
        if head < z1:
            box(xa, y0, head, xb, y1, z1, material, tag + "_lintel")
        x = xb
    if x < x_to:
        box(x, y0, z0, x_to, y1, z1, material, tag)


def wall_y(x0, x1, y_from, y_to, z0, z1, material, tag, openings=()):
    """Wall running along Y with thickness x0..x1."""
    cuts = sorted(openings)
    y = y_from
    for (ya, yb, sill, head) in cuts:
        if ya > y:
            box(x0, y, z0, x1, ya, z1, material, tag)
        if sill > z0:
            box(x0, ya, z0, x1, yb, sill, material, tag + "_sill")
        if head < z1:
            box(x0, ya, head, x1, yb, z1, material, tag + "_lintel")
        y = yb
    if y < y_to:
        box(x0, y, z0, x1, y_to, z1, material, tag)


# --- Yard ------------------------------------------------------------------
box(-24, -24, -0.5, 24, 24, 0.0, "dirt", "ground")
H = 4.0
box(-24, 23.5, 0, 24, 24, H, "concrete", "perimeter")
box(-24, -24, 0, 24, -23.5, H, "concrete", "perimeter")
box(23.5, -23.5, 0, 24, 23.5, H, "concrete", "perimeter")
box(-24, -23.5, 0, -23.5, 23.5, H, "concrete", "perimeter")

# --- Building: x -6..6, y -5..5, two floors --------------------------------
F1, SLAB, F2, ROOF = 3.0, 3.3, 6.0, 6.3
T = 0.3  # exterior wall thickness
DOOR, WIN_SILL, WIN_HEAD = 2.2, 1.0, 2.0
# ground floor slab (slightly raised concrete pad: 0.1 m, walkable step)
box(-6, -5, 0, 6, 5, 0.1, "concrete", "floor")
G = 0.1
# Ground floor exterior walls (brick)
wall_x(-5, -5 + T, -6, 6, G, F1, "brick", "wall_s1",
       [(-1, 1, G, DOOR), (3, 5, WIN_SILL, WIN_HEAD)])
wall_x(5 - T, 5, -6, 6, G, F1, "brick", "wall_n1",
       [(-4, -2, WIN_SILL, WIN_HEAD), (2.0, 3.6, G, DOOR)])
wall_y(-6, -6 + T, -5 + T, 5 - T, G, F1, "brick", "wall_w1", [(-1, 1, G, DOOR)])
wall_y(6 - T, 6, -5 + T, 5 - T, G, F1, "brick", "wall_e1", [(-1.5, 0.5, WIN_SILL, WIN_HEAD)])
# Interior drywall partition with a doorway
wall_y(-0.05, 0.05, -5 + T, 1.8, G, F1, "drywall", "partition", [(-3.2, -2.2, G, DOOR)])

# Staircase along the north wall, rising toward +X: 13 steps to the upper floor
STEPS = 13
SX0, SX1, SY0, SY1 = 0.8, 4.8, 2.6, 4.7
tread = (SX1 - SX0) / STEPS
for i in range(STEPS):
    top = G + (SLAB - G) * (i + 1) / STEPS
    box(SX0 + tread * i, SY0, G, SX1, SY1, top, "wood", "stairs")

# Upper floor slab with a hole above the stairs
box(-6, -5, F1, SX0, 5, SLAB, "concrete", "slab")
box(SX1, -5, F1, 6, 5, SLAB, "concrete", "slab")
box(SX0, -5, F1, SX1, SY0, SLAB, "concrete", "slab")
box(SX0, SY1, F1, SX1, 5, SLAB, "concrete", "slab")
# Stair railing on the open side (upper floor), waist-high wood
box(SX0, SY0 - 0.08, SLAB, SX1 - 0.9, SY0, SLAB + 1.0, "wood", "railing")

# Upper floor exterior walls (drywall on studs outside = wood siding)
wall_x(-5, -5 + T, -6, 6, SLAB, F2, "wood", "wall_s2",
       [(-4, -2, SLAB + 0.9, SLAB + 2.0), (2, 4, SLAB + 0.9, SLAB + 2.0)])
wall_x(5 - T, 5, -6, 6, SLAB, F2, "wood", "wall_n2", [(-3, -1, SLAB + 0.9, SLAB + 2.0)])
wall_y(-6, -6 + T, -5 + T, 5 - T, SLAB, F2, "wood", "wall_w2", [(-1, 1, SLAB + 0.9, SLAB + 2.0)])
wall_y(6 - T, 6, -5 + T, 5 - T, SLAB, F2, "wood", "wall_e2", [(-3, -1, SLAB + 0.9, SLAB + 2.0)])
box(-6, -5, F2, 6, 5, ROOF, "concrete", "roof")

# --- Yard cover --------------------------------------------------------------
# Shipping container (metal) with an open end facing west
box(10, -12, 0, 16, -11.9, 2.6, "metal", "container")
box(10, -9.6, 0, 16, -9.5, 2.6, "metal", "container")
box(15.9, -11.9, 0, 16, -9.6, 2.6, "metal", "container")
box(10, -12, 2.5, 16, -9.5, 2.6, "metal", "container_roof")
box(10, -12, 0, 16, -9.5, 0.05, "metal", "container_floor")
# Wooden crates
for (cx, cy, s) in [(-12, 8, 1.2), (-10.6, 8.2, 1.0), (12, 9, 1.2), (12, 10.3, 1.2), (-14, -9, 1.2)]:
    box(cx - s / 2, cy - s / 2, 0, cx + s / 2, cy + s / 2, s, "wood", "crate")
box(12 - 0.6, 9 - 0.6, 1.2, 12 + 0.6, 9 + 0.6, 2.4, "wood", "crate")
# Concrete jersey barriers (waist-high cover)
for (cx, cy, along_x) in [(-9, -3, False), (9, 3, False), (0, -14, True), (0, 14, True), (-17, 14, True)]:
    if along_x:
        box(cx - 1.5, cy - 0.3, 0, cx + 1.5, cy + 0.3, 0.9, "concrete", "barrier")
    else:
        box(cx - 0.3, cy - 1.5, 0, cx + 0.3, cy + 1.5, 0.9, "concrete", "barrier")
# Low wall that must be jumped (0.4 m > step height)
box(-18, -4, 0, -17.6, 4, 0.4, "brick", "low_wall")
# Steps test: three rising steps leading onto a platform
for i in range(3):
    box(17 + i * 0.4, 14, 0, 20, 18, 0.3 * (i + 1), "concrete", "steps")
box(20, 14, 0, 23.5, 18, 0.9, "concrete", "platform")

spawns = [
    {"pos": [-20.0, 0.0, 0.0], "yaw": 0.0, "team": 0},
    {"pos": [-20.0, 6.0, 0.0], "yaw": 0.0, "team": 0},
    {"pos": [-20.0, -6.0, 0.0], "yaw": 0.0, "team": 0},
    {"pos": [20.0, 0.0, 0.0], "yaw": 180.0, "team": 1},
    {"pos": [20.0, -6.0, 0.0], "yaw": 180.0, "team": 1},
    {"pos": [20.0, 6.0, 0.0], "yaw": 180.0, "team": 1},
]

doc = {"name": "test_arena", "kill_z": -30.0, "solids": solids, "spawns": spawns}
out = os.path.join(os.path.dirname(__file__), "..", "..", "Shared", "Definitions", "maps", "test_arena.json")
with open(out, "w", newline="\n") as f:
    f.write(json.dumps(doc, indent=1) + "\n")
print(f"wrote {os.path.normpath(out)} with {len(solids)} solids")
