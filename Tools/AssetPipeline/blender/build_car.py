"""Four-door sedan, 4.5 m x 1.8 m x 1.45 m (front along +Y).

The body is lofted from superellipse sections along the car (wheel arches
are part of the side profile), the greenhouse from tapered sections, and
both are merged by a voxel remesh. Paint by region: glass with A/B/C
pillars, head and tail lights, grille, plates, bumpers, door seams and
handles; mirrors; tyres on five-spoke rims with brake discs.

blender -b --factory-startup --python Tools/AssetPipeline/blender/build_car.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402
import tfps_common as tc  # noqa: E402

ASSET = "Sedan"
HALF_L = 2.25
WHEEL_Y = 1.35
WHEEL_R = 0.32
ARCH_R = 0.42
BELT = 0.95

PAINT = ((0.36, 0.38, 0.41), 0.55, 0.32)
GLASS = ((0.015, 0.02, 0.025), 0.0, 0.04)
TRIM = ((0.02, 0.02, 0.022), 0.0, 0.55)
PLASTIC = ((0.035, 0.035, 0.037), 0.0, 0.7)
HEAD = ((0.85, 0.86, 0.86), 0.2, 0.08)
TAIL = ((0.45, 0.01, 0.01), 0.0, 0.12)
PLATE = ((0.8, 0.8, 0.76), 0.0, 0.4)
RUBBER = ((0.018, 0.018, 0.018), 0.0, 0.85)
RIM = ((0.55, 0.56, 0.58), 0.9, 0.3)
DISC = ((0.2, 0.19, 0.18), 0.8, 0.5)


def bottom(y):
    z = 0.2
    for wy in (-WHEEL_Y, WHEEL_Y):
        d = abs(y - wy)
        if d < ARCH_R:
            z = max(z, WHEEL_R + math.sqrt(ARCH_R * ARCH_R - d * d))
    if abs(y) > HALF_L - 0.2:   # bumpers sweep up a little at the ends
        z = max(z, 0.2 + (abs(y) - (HALF_L - 0.2)) * 0.35)
    return z


def top(y):
    if y > 0.95:   # hood, down to the nose
        t = (y - 0.95) / (HALF_L - 0.95)
        return 0.93 - 0.08 * t - 0.06 * t ** 6
    if y < -1.15:   # boot deck, down to the tail
        t = (-1.15 - y) / (HALF_L - 1.15)
        return 0.99 - 0.02 * t - 0.06 * t ** 6
    return BELT + (0.04 * (-y - 0.0) / 1.15 if y < 0 else 0.0)


def half_width(y):
    a = abs(y)
    return 0.9 - (0.07 * ((a - 1.6) / 0.65) ** 3 if a > 1.6 else 0.0)


def roof(y):
    """Greenhouse top: windshield, roof, rear glass."""
    if y > 0.15:
        return BELT + (1.43 - BELT) * max(0.0, (0.98 - y) / 0.83) ** 0.8
    if y < -0.7:
        return 0.99 + (1.43 - 0.99) * max(0.0, (y + 1.22) / 0.52) ** 0.8
    return 1.43 + 0.02 * math.cos((y + 0.27) / 0.43 * math.pi * 0.5)


def superellipse_ring(cx, cz, w, h, n=3.2, steps=36):
    pts = []
    for k in range(steps):
        a = 2 * math.pi * k / steps
        c, s = math.cos(a), math.sin(a)
        x = w * math.copysign(abs(c) ** (2 / n), c)
        z = h * math.copysign(abs(s) ** (2 / n), s)
        pts.append((cx + x, cz + z))
    return pts


def loft_sections(name, sections):
    """sections: [(y, [(x, z), ...])] with equal point counts; capped ends."""
    bm = bmesh.new()
    rings = [[bm.verts.new((x, y, z)) for x, z in pts] for y, pts in sections]
    n = len(sections[0][1])
    for r0, r1 in zip(rings, rings[1:]):
        for k in range(n):
            bm.faces.new((r0[k], r0[(k + 1) % n], r1[(k + 1) % n], r1[k]))
    bm.faces.new(rings[0][::-1])
    bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return o


def body_shell():
    secs = []
    steps = 90
    for i in range(steps + 1):
        y = -HALF_L + 2 * HALF_L * i / steps
        b, t = bottom(y), top(y)
        secs.append((y, superellipse_ring(0.0, (b + t) / 2, half_width(y), (t - b) / 2, n=5.0)))
    body = loft_sections("body", secs)
    cab = []
    for i in range(41):
        y = 1.0 - 2.25 * i / 40
        r = roof(y)
        b = BELT - 0.08
        h = max(r - b, 0.03)
        ring = []
        for x, z in superellipse_ring(0.0, b + h / 2, 1.0, h / 2, n=4.0):
            f = (z - b) / h
            ring.append((x * (0.85 - 0.13 * f), z))
        cab.append((y, ring))
    cabin = loft_sections("cabin", cab)
    o = tc.join([body, cabin], "shell")
    m = o.modifiers.new("Remesh", 'REMESH')
    m.mode = 'VOXEL'
    m.voxel_size = 0.014
    tc.apply_modifiers(o)
    sm = o.modifiers.new("Smooth", 'CORRECTIVE_SMOOTH')
    sm.factor = 0.5
    sm.iterations = 6
    sm.use_only_smooth = True
    tc.apply_modifiers(o)
    dec = o.modifiers.new("Decimate", 'DECIMATE')
    dec.ratio = min(1.0, 110000 / max(1, len(o.data.polygons)))   # dense enough for crisp paint regions
    tc.apply_modifiers(o)
    return o


def region(co, nrm):
    ax, y, z = abs(co.x), co.y, co.z
    front, back = nrm.y > 0.55, nrm.y < -0.55
    # Greenhouse: side windows (B pillar, roof rail, A/C pillars at their
    # ends, trim at the sill), windshield and rear glass (pillars at the
    # edges), roof panel.
    if z > top(y) + 0.02 and -1.24 < y < 1.0:
        if nrm.z > 0.85 and -0.75 < y < 0.2:
            return PAINT
        if abs(nrm.x) > 0.6:
            if abs(y + 0.27) < 0.05:
                return TRIM
            if y > 0.86 or y < -1.12 or z > roof(y) - 0.045:
                return PAINT
            if z < BELT + 0.05:
                return TRIM
            return GLASS
        if ax > 0.6:
            return PAINT
        return GLASS
    # Bumpers (lights, plates and grille are separate parts).
    if z < 0.4 and (front or back or abs(y) > HALF_L - 0.3):
        return PLASTIC
    if z < 0.3:
        return PLASTIC
    # Door seams and handles on the sides.
    if abs(nrm.x) > 0.7 and 0.3 < z < BELT:
        for sy in (0.97, -0.27, -1.18):
            if abs(y - sy) < 0.006:
                return TRIM
        if 0.84 < z < 0.88 and (abs(y - 0.55) < 0.07 or abs(y + 0.62) < 0.07):
            return TRIM
    return PAINT


def wheel(name, x, y):
    side = 1 if x > 0 else -1
    parts = []
    tyre = tc.cylinder(f"{name}_tyre", WHEEL_R, 0.22, (x, y, WHEEL_R), 'X', 40, 0.06)
    parts.append(tc.paint(tyre, *RUBBER))
    face = x + side * 0.1
    disc = tc.cylinder(f"{name}_disc", 0.15, 0.03, (x, y, WHEEL_R), 'X', 28, 0.004)
    parts.append(tc.paint(disc, *DISC))
    rim_ring = tc.cylinder(f"{name}_rim", 0.205, 0.04, (face, y, WHEEL_R), 'X', 32, 0.01)
    parts.append(tc.paint(rim_ring, *RIM))
    hub = tc.cylinder(f"{name}_hub", 0.05, 0.05, (face + side * 0.01, y, WHEEL_R), 'X', 16, 0.01)
    parts.append(tc.paint(hub, *RIM))
    for k in range(5):
        a = 2 * math.pi * k / 5
        spoke = tc.box(f"{name}_spoke{k}", (0.025, 0.035, 0.17), (face + side * 0.005, y, WHEEL_R), bevel=0.006)
        spoke.rotation_euler = (a, 0, 0)
        bpy.context.view_layer.objects.active = spoke
        spoke.location = Vector((face + side * 0.005, y, WHEEL_R)) + Vector((0, -math.sin(a) * 0.1, math.cos(a) * 0.1))
        parts.append(tc.paint(spoke, *RIM))
    return parts


def build():
    tc.reset_scene()
    shell = body_shell()
    me = shell.data
    col = me.color_attributes.new("tfps_col", 'FLOAT_COLOR', 'CORNER')
    mr = me.color_attributes.new("tfps_mr", 'FLOAT_COLOR', 'CORNER')
    per_vertex = [region(v.co, v.normal) for v in me.vertices]
    for li, loop in enumerate(me.loops):
        rgb, metallic, rough = per_vertex[loop.vertex_index]
        col.data[li].color = (rgb[0], rgb[1], rgb[2], 1.0)
        mr.data[li].color = (metallic, rough, 0.0, 1.0)
    parts = [shell]
    for sx in (-1, 1):
        mirror = tc.box(f"mirror{sx}", (0.1, 0.06, 0.09), (sx * 0.96, 0.82, 1.02), bevel=0.02)
        parts.append(tc.paint(mirror, *PAINT))
        arm = tc.box(f"mirror_arm{sx}", (0.08, 0.03, 0.02), (sx * 0.9, 0.84, 1.0), bevel=0.005)
        parts.append(tc.paint(arm, *TRIM))
        for wy in (-WHEEL_Y, WHEEL_Y):
            parts += wheel(f"w{sx}{wy}", sx * 0.78, wy)
    # Lamps, plates and grille on the flat end faces.
    for sx in (-1, 1):
        head = tc.box(f"headlight{sx}", (0.34, 0.03, 0.11), (sx * 0.6, HALF_L + 0.008, 0.72), bevel=0.015)
        parts.append(tc.paint(head, *HEAD))
        bezel = tc.box(f"head_bezel{sx}", (0.37, 0.02, 0.14), (sx * 0.6, HALF_L + 0.002, 0.72), bevel=0.01)
        parts.append(tc.paint(bezel, *TRIM))
        tail = tc.box(f"taillight{sx}", (0.36, 0.03, 0.1), (sx * 0.6, -HALF_L - 0.008, 0.84), bevel=0.012)
        parts.append(tc.paint(tail, *TAIL))
        rev = tc.box(f"reverse{sx}", (0.08, 0.032, 0.1), (sx * 0.38, -HALF_L - 0.008, 0.84), bevel=0.01)
        parts.append(tc.paint(rev, *HEAD))
    grille = tc.box("grille", (0.7, 0.025, 0.16), (0, HALF_L + 0.006, 0.58), bevel=0.01)
    parts.append(tc.paint(grille, *TRIM))
    for k in range(4):
        slat = tc.box(f"slat{k}", (0.66, 0.03, 0.012), (0, HALF_L + 0.012, 0.52 + k * 0.04))
        parts.append(tc.paint(slat, *RIM))
    for yy, zz in ((HALF_L + 0.006, 0.4), (-HALF_L - 0.006, 0.58)):
        plate = tc.box(f"plate{yy}", (0.52, 0.012, 0.11), (0, yy, zz), bevel=0.004)
        parts.append(tc.paint(plate, *PLATE))
    under = tc.box("underbody", (1.5, 3.4, 0.04), (0, 0, 0.21))
    parts.append(tc.paint(under, *PLASTIC))
    car = tc.join(parts, ASSET)
    tc.set_origin_floor_center(car)
    tc.shade_auto_smooth(car, 50.0)
    tc.uv_unwrap(car, 0.003)
    mat = tc.painted_material(f"M_{ASSET}_Proc", grain=0.03, bump_strength=0.05)
    car.data.materials.append(mat)
    lod1 = tc.make_lod(car, 0.35, f"{ASSET}_LOD1")
    return tc.finish_asset(ASSET, car, lod1, [], mat,
                           notes="Dresses 'car' solids (1.8 x 4.5 m body box 0.25-1.0 m up, cabin above); "
                                 "origin at the ground, front along +Y (Godot -Z).")


if __name__ == "__main__":
    build()
