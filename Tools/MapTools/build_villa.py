#!/usr/bin/env python3
"""Generates Shared/Definitions/maps/villa.json — "Hillside Villa".

A detached two-storey house (18 x 14 m) with a basement under its east
half, set in a walled garden with a shed, a terrace and parked cars.
Ground floor: living room (west), kitchen (north-east), dining room
(south-east) with the stairs. Upper floor (wooden floor in the west half,
breakable from below): master bedroom, bathroom, study, kid's room.
Basement: storage and wine cellar.

Objective sites: "Master Bedroom" (upper floor) and "Basement".
Run: python3 Tools/MapTools/build_villa.py
"""
from mapkit import Map, door, window

m = Map("villa")

HX0, HX1, HY0, HY1 = -8.0, 10.0, -7.0, 7.0  # house footprint
BX0 = 0.0                                    # basement spans x 0..10
T, DT = 0.3, 0.12
G = 0.1                                       # ground floor top
F1, SLAB = 3.0, 3.35                          # upper floor top
F2, ROOF = 6.2, 6.5
BF = -3.2                                     # basement floor top

# --- Ground (with a hole for the basement) and the garden wall ---------------------
m.floor(-40, -36, 40, 30, 0.0, 0.5, "dirt", "ground", holes=[(BX0, HY0, HX1, HY1, None)])
for (x0, y0, x1, y1) in [(-30, 20, 30, 20.3), (-30, -26, -6, -25.7), (6, -26, 30, -25.7),
                         (-30.3, -26, -30, 20.3), (30, -26, 30.3, 20.3)]:
    m.box(x0, y0, 0, x1, y1, 1.3, "brick", "garden_wall")

# --- Basement: concrete box under the east half ------------------------------------
m.box(BX0, HY0, BF - 0.3, HX1, HY1, BF, "concrete", "basement_floor")
m.box(BX0, HY0, BF, HX1, HY0 + T, 0.0, "concrete", "basement_wall")
m.box(BX0, HY1 - T, BF, HX1, HY1, 0.0, "concrete", "basement_wall")
m.box(BX0, HY0 + T, BF, BX0 + T, HY1 - T, 0.0, "concrete", "basement_wall")
m.box(HX1 - T, HY0 + T, BF, HX1, HY1 - T, 0.0, "concrete", "basement_wall")
# Wine cellar partition (brick) with an open doorway, wine racks.
m.wall("x", -0.06, 0.06, BX0 + T, HX1 - T, BF, 0.0, "brick", "cellar_wall", [door(5.0, fill=None)])
for x in [1.5, 4.0, 7.5]:
    m.box(x, -6.5, BF, x + 1.6, -6.0, BF + 2.0, "wood", "wine_rack")
m.crate(8.5, 3.0, 1.0, BF)
m.crate(8.5, 4.1, 1.0, BF)
# Basement stairs (rising toward -x, top at the ground floor near x = 3).
m.stairs(3.0, 5.0, 7.0, 6.5, BF, G, "-x")

# --- Ground floor ---------------------------------------------------------------------
m.floor(HX0, HY0, HX1, HY1, G, 0.3, "concrete", "floor",
        holes=[(3.0, 5.0, 7.0, 6.5, None)])
# Exterior walls (brick).
m.wall("x", HY0, HY0 + T, HX0, HX1, G, F1, "brick", "wall_s1",
       [window(-6.0, 1.4), door(-3.0), window(1.0, 1.4), window(6.5, 1.6)])
m.wall("x", HY1 - T, HY1, HX0, HX1, G, F1, "brick", "wall_n1",
       [window(-5.5, 1.6), window(-1.5, 1.6), door(8.5)])
m.wall("y", HX0, HX0 + T, HY0 + T, HY1 - T, G, F1, "brick", "wall_w1",
       [window(-3.5, 1.4), window(3.5, 3.0, sill=0.0, head=2.2)])  # patio glass doors
m.wall("y", HX1 - T, HX1, HY0 + T, HY1 - T, G, F1, "brick", "wall_e1",
       [window(-2.5, 1.4), window(3.0, 1.4)])
# Interior: living room | east half (drywall), kitchen | dining.
m.wall("y", 2.0 - DT / 2, 2.0 + DT / 2, HY0 + T, HY1 - T, G, F1, "drywall", "partition",
       [door(-2.0, 1.4, fill=None), door(3.6, fill=None)])
m.wall("x", -DT / 2, DT / 2, 2.0 + DT / 2, HX1 - T, G, F1, "drywall", "partition",
       [door(8.4, 1.2, fill=None)])
# Furniture (cover).
m.box(-6.5, 1.0, G, -4.5, 2.0, G + 0.8, "wood", "sofa")
m.box(-3.5, -4.5, G, -1.5, -3.5, G + 0.8, "wood", "sofa")
m.box(4.0, 1.5, G, 8.0, 2.4, G + 0.95, "wood", "kitchen_island")
m.box(9.1, 1.0, G, 9.7, 6.4, G + 0.95, "wood", "counter")
m.box(5.0, -3.5, G, 7.5, -2.2, G + 0.75, "wood", "dining_table")

# Stairs to the upper floor along the south wall, rising toward +x.
m.stairs(3.0, -6.7, 8.0, -5.2, G, SLAB, "+x")

# --- Upper floor ------------------------------------------------------------------------
# West half wood (breakable from below), east half concrete; hole over the stairs.
m.floor(HX0, HY0, 2.0, HY1, SLAB, SLAB - F1, "wood", "floor_wood")
m.floor(2.0, HY0, HX1, HY1, SLAB, SLAB - F1, "concrete", "slab",
        holes=[(3.0, -6.7, 8.0, -5.2, None)])
m.box(3.0, -5.2, SLAB, 7.2, -5.12, SLAB + 1.0, "wood", "railing")
# Exterior walls (wooden siding).
m.wall("x", HY0, HY0 + T, HX0, HX1, SLAB, F2, "wood", "wall_s2",
       [window(-5.0, 1.4), window(-0.5, 1.4), window(5.0, 1.4)])
m.wall("x", HY1 - T, HY1, HX0, HX1, SLAB, F2, "wood", "wall_n2",
       [window(-5.0, 2.0), window(0.0, 1.2), window(6.0, 1.6)])
m.wall("y", HX0, HX0 + T, HY0 + T, HY1 - T, SLAB, F2, "wood", "wall_w2",
       [window(-4.0, 1.0), window(3.5, 1.6)])
m.wall("y", HX1 - T, HX1, HY0 + T, HY1 - T, SLAB, F2, "wood", "wall_e2",
       [window(3.0, 1.4)])
# Interior walls (drywall): master bedroom north-west; bathroom and study south-west;
# kid's room north-east; landing south-east.
m.wall("x", -DT / 2, DT / 2, HX0 + T, HX1 - T, SLAB, F2, "drywall", "partition2",
       [door(-5.5, fill=None), door(-1.0, fill=None), door(3.5, fill=None)])
m.wall("y", -3.0 - DT / 2, -3.0 + DT / 2, HY0 + T, -DT / 2, SLAB, F2, "drywall", "partition2",
       [door(-3.5, fill="door")])
m.wall("y", 2.0 - DT / 2, 2.0 + DT / 2, DT / 2, HY1 - T, SLAB, F2, "drywall", "partition2",
       [door(3.5, fill=None)])
m.box(-7.5, 3.5, SLAB, -5.5, 6.5, SLAB + 0.6, "wood", "bed")
m.box(-1.5, 5.8, SLAB, 1.5, 6.6, SLAB + 2.0, "wood", "wardrobe")
m.box(6.5, 4.5, SLAB, 8.5, 6.5, SLAB + 0.6, "wood", "bed")
m.box(-2.5, -6.6, SLAB, -0.5, -5.8, SLAB + 0.75, "wood", "desk")
m.box(HX0, HY0, F2, HX1, HY1, ROOF, "concrete", "roof")

# --- Garden -------------------------------------------------------------------------------
# Terrace with a low wall on the west side.
m.box(-14.0, -4.0, 0, -8.0, 7.0, 0.15, "concrete", "terrace")
m.box(-14.0, -4.0, 0.15, -13.7, 7.0, 0.9, "brick", "terrace_wall")
# Garden shed (wood) near the north-west corner.
m.wall("x", 14.0, 14.15, -24.0, -19.0, 0, 2.4, "wood", "shed", [door(-21.5)])
m.box(-24.0, 17.85, 0, -19.0, 18.0, 2.4, "wood", "shed")
m.box(-24.0, 14.15, 0, -23.85, 17.85, 2.4, "wood", "shed")
m.box(-19.15, 14.15, 0, -19.0, 17.85, 2.4, "wood", "shed")
m.box(-24.0, 14.0, 2.4, -19.0, 18.0, 2.55, "wood", "shed_roof")
# Parked cars in the driveway (south-east) and hedges.
for (x, y) in [(14.0, -16.0), (18.5, -16.0)]:
    m.box(x - 0.9, y - 2.25, 0.25, x + 0.9, y + 2.25, 1.0, "metal", "car")
    m.box(x - 0.8, y - 1.0, 1.0, x + 0.8, y + 1.2, 1.45, "metal", "car_cabin")
for (x0, y0, x1, y1) in [(-26, -12, -16, -11), (16, 4, 17, 14), (-4, -20, 4, -19)]:
    m.box(x0, y0, 0, x1, y1, 1.6, "wood", "hedge", destructible=False)
for (x, y) in [(-12.0, -14.0), (12.0, 10.0), (22.0, 0.0)]:
    m.crate(x, y, 1.0, 0.0, "concrete")

# --- Spawns and objectives ----------------------------------------------------------------
for (x, y) in [(-12.0, -23.0), (-8.0, -23.0), (0.0, -23.0), (8.0, -23.0), (12.0, -23.0)]:
    m.spawn(x, y, 0.0, 90.0, 0)
for (x, y, z) in [(-5.0, -2.0, G), (6.0, 4.5, G), (-4.0, 1.5, SLAB), (5.0, 2.5, SLAB), (6.0, 3.0, BF)]:
    m.spawn(x, y, z, 270.0, 1)
m.objective("Master Bedroom", HX0 + T, DT / 2, SLAB, 1.9, HY1 - T, F2)
m.objective("Basement", BX0 + T, HY0 + T, BF, HX1 - T, HY1 - T, -0.2)
m.write()
