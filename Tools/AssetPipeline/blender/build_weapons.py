"""Weapons: carbine (AR-7), SMG (K-9), marksman rifle (M-308), pistol (P-9)
and pump shotgun (SG-12). Original designs at real-world proportions:
Picatinny rails with teeth, M-LOK slots cut through the handguards,
slotted flash hiders, ejection ports, forward assist, charging handles,
curved ribbed magazines, adjustable stocks on buffer tubes, optics with
open tubes, folding sights, selectors, pins and trigger guards.

Finishes: anodised aluminium, parkerised steel, polymer and FDE
furniture. Edge wear comes from surface curvature (lighter bare metal on
sharp edges of worn parts) and a bevel shader rounds every edge in the
baked normal map. 1024 px textures.

Built along +X (barrel forward) with Z up, then turned so the barrel points
along +Y: Godot imports Blender +Y as -Z, its forward axis. The manifest
records the grip (the top of the pistol grip, where the hand holds it), the
muzzle and the sight, in the exported model's coordinates (metres, Blender
axes), so the game can hold the weapon, place muzzle flashes and aim.

blender -b --factory-startup --python Tools/AssetPipeline/blender/build_weapons.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

os.environ.setdefault("TFPS_TEX_SIZE", "1024")
import tfps_common as tc  # noqa: E402

# (rgb linear, metallic, roughness, wear 0..1)
ANOD = ((0.028, 0.028, 0.03), 0.55, 0.42, 0.9)      # anodised aluminium
PARK = ((0.034, 0.034, 0.033), 0.7, 0.55, 0.6)      # parkerised steel
BLUED = ((0.02, 0.022, 0.026), 0.8, 0.32, 0.7)      # blued steel
POLY = ((0.022, 0.022, 0.021), 0.0, 0.72, 0.15)     # black polymer
RUBBER = ((0.015, 0.015, 0.015), 0.0, 0.92, 0.0)
FDE = ((0.2, 0.15, 0.088), 0.0, 0.7, 0.25)          # flat dark earth polymer
ODG = ((0.075, 0.085, 0.05), 0.0, 0.72, 0.25)       # olive drab polymer
GLASS = ((0.01, 0.02, 0.02), 0.6, 0.05, 0.0)
BRASS = ((0.55, 0.38, 0.16), 1.0, 0.35, 0.0)
SIGHT_DOT = ((0.9, 0.85, 0.2), 0.0, 0.4, 0.0)       # tritium / paint dot
WOOD = ((0.16, 0.08, 0.035), 0.0, 0.55, 0.3)


def part(o, finish):
    rgb, metallic, rough, wear = finish
    me = o.data
    for name, value in (("tfps_col", (rgb[0], rgb[1], rgb[2], 1.0)), ("tfps_mr", (metallic, rough, wear, 1.0))):
        attr = me.color_attributes.get(name) or me.color_attributes.new(name, 'FLOAT_COLOR', 'CORNER')
        for d in attr.data:
            d.color = value
    return o


def profile(name, pts, thickness, finish, bevel=0.0015):
    return part(tc.extrude_profile(name, pts, thickness, bevel), finish)


def bx(name, size, centre, finish, bevel=0.001, rot=None):
    o = tc.box(name, size, centre, bevel)
    if rot:
        o.rotation_euler = [math.radians(a) for a in rot]
        tc.select_only([o])
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    return part(o, finish)


def cyl(name, r, length, centre, finish, verts=24, bevel=0.0, axis='X'):
    return part(tc.cylinder(name, r, length, centre, axis, verts, bevel), finish)


def tube(name, r, length, centre, finish, wall=0.0022, verts=32):
    """Open tube along X (no end caps), so the eye can look through it."""
    o = tc.cylinder(name, r, length, centre, 'X', verts)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    caps = [f for f in bm.faces if abs(f.normal.x) > 0.9]
    bmesh.ops.delete(bm, geom=caps, context='FACES_ONLY')
    bm.to_mesh(o.data)
    bm.free()
    m = o.modifiers.new("Solidify", 'SOLIDIFY')
    m.thickness = wall
    m.offset = -1.0
    tc.apply_modifiers(o)
    return part(o, finish)


def cut(target, cutters):
    """Boolean difference of each cutter from target (cutters are removed)."""
    for c in cutters:
        m = target.modifiers.new("Cut", 'BOOLEAN')
        m.operation = 'DIFFERENCE'
        m.solver = 'EXACT'
        m.object = c
        tc.apply_modifiers(target)
        bpy.data.objects.remove(c, do_unlink=True)
    return target


def slot(centre, size, axis='Y'):
    """Rounded slot cutter (an M-LOK slot or a vent), long along X."""
    o = tc.box("slot", size, centre, min(size[0], size[2] if axis == 'Y' else size[1]) * 0.45, 3)
    return o


def rail(x0, x1, z, finish, width=0.021, y=0.0):
    """Picatinny rail along X from x0 to x1 with its top at z: base plus
    cross teeth every 10 mm."""
    parts = [bx("rail_base", (x1 - x0, width * 0.78, 0.0045), ((x0 + x1) / 2, y, z - 0.0055), finish, 0.0008)]
    n = int((x1 - x0) / 0.01)
    for i in range(n):
        x = x0 + 0.005 + i * 0.01
        parts.append(bx("rail_tooth", (0.0052, width, 0.0055), (x, y, z - 0.00275), finish, 0.0006))
    return parts


def octagon_handguard(x0, x1, cz, r, finish, slots_per_side=4):
    """Free-float handguard: octagonal tube with M-LOK slots on 3, 6 and 9
    o'clock."""
    o = tc.cylinder("handguard", r, x1 - x0, ((x0 + x1) / 2, 0, cz), 'X', 8, 0.0)
    o.rotation_euler = (math.radians(22.5), 0, 0)
    tc.select_only([o])
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    # Hollow it.
    inner = tc.cylinder("bore", r - 0.0025, x1 - x0 + 0.01, ((x0 + x1) / 2, 0, cz), 'X', 8, 0.0)
    inner.rotation_euler = (math.radians(22.5), 0, 0)
    tc.select_only([inner])
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    cut(o, [inner])
    cutters = []
    span = (x1 - x0) - 0.04
    for i in range(slots_per_side):
        x = x0 + 0.03 + span * (i + 0.5) / slots_per_side
        cutters.append(slot((x, r, cz), (0.032, 0.02, 0.007)))
        cutters.append(slot((x, -r, cz), (0.032, 0.02, 0.007)))
        cutters.append(tc.box("slot", (0.032, 0.007, 0.02), (x, 0, cz - r), 0.003, 3))
    cut(o, cutters)
    m = o.modifiers.new("Bevel", 'BEVEL')
    m.width = 0.0012
    m.segments = 2
    m.limit_method = 'ANGLE'
    tc.apply_modifiers(o)
    return part(o, finish)


def flash_hider(x, z, finish, r=0.0105, length=0.055, prongs=3):
    o = tc.cylinder("flash_hider", r, length, (x, 0, z), 'X', 24, 0.0008)
    bore = tc.cylinder("fh_bore", r * 0.62, length + 0.01, (x, 0, z), 'X', 20)
    cut(o, [bore])
    cutters = []
    for k in range(prongs):
        a = 2 * math.pi * k / prongs + math.pi / 2
        c = tc.box("fh_slot", (length * 0.6, 0.004, r * 2.2), (x + length * 0.25, 0, z), 0.0)
        c.rotation_euler = (a, 0, 0)
        tc.select_only([c])
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
        # Only one side: shift the slot out along its direction.
        c.location = (0, math.cos(a) * r * 0.9, math.sin(a) * r * 0.9)
        tc.select_only([c])
        bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
        cutters.append(c)
    cut(o, cutters)
    return part(o, finish)


def magazine_curved(x_front, x_back, top, depth, curve, width, finish, ribs=True, name="mag"):
    """Curved box magazine (top at z=top) with side ribs and a base plate."""
    pts = []
    steps = 8
    for i in range(steps + 1):
        t = i / steps
        z = top - depth * t
        dx = curve * t * t
        pts.append((x_front + dx, z))
    back = []
    for i in range(steps, -1, -1):
        t = i / steps
        z = top - depth * t
        dx = curve * t * t
        back.append((x_back + dx * 1.1, z))
    parts = [profile(name, pts + back, width, finish, 0.002)]
    if ribs:
        for i in range(1, 6):
            t = i / 6.5
            z = top - depth * t
            dx = curve * t * t
            cx = (x_front + x_back) / 2 + dx * 1.05
            parts.append(bx("mag_rib", (abs(x_front - x_back) * 0.8, width + 0.002, 0.0035), (cx, 0, z), finish, 0.0012))
    cx = (x_front + x_back) / 2 + curve * 1.05
    parts.append(bx("mag_base", (abs(x_front - x_back) + 0.01, width + 0.004, 0.012), (cx, 0, top - depth - 0.004), finish, 0.002))
    for o in parts:
        g = o.vertex_groups.new(name="mag")
        g.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
    return parts


def red_dot(x, z, finish=ANOD):
    """Tube red-dot sight on a cantilever mount, open front and back."""
    return [
        bx("rd_mount", (0.05, 0.022, 0.012), (x, 0, z + 0.006), finish, 0.0015),
        bx("rd_riser", (0.026, 0.018, 0.012), (x, 0, z + 0.016), finish, 0.0015),
        tube("rd_body", 0.017, 0.062, (x, 0, z + 0.038), finish, 0.0028),
        tube("rd_hood", 0.0195, 0.012, (x + 0.034, 0, z + 0.038), finish, 0.0025),
        tube("rd_lens_ring", 0.0175, 0.004, (x - 0.03, 0, z + 0.038), RUBBER, 0.0015),
        cyl("rd_turret_top", 0.0075, 0.012, (x, 0, z + 0.06), finish, 16, 0.001, 'Z'),
        cyl("rd_turret_side", 0.0075, 0.012, (x, -0.022, z + 0.038), finish, 16, 0.001, 'Y'),
        bx("rd_lever", (0.012, 0.006, 0.01), (x + 0.012, 0.016, z + 0.006), PARK, 0.0015),
    ]


def scope(x, z, length=0.32, finish=ANOD):
    tz = z + 0.042
    return [
        bx("ring_base_a", (0.022, 0.024, 0.01), (x - length * 0.22, 0, z + 0.005), finish, 0.0015),
        bx("ring_base_b", (0.022, 0.024, 0.01), (x + length * 0.22, 0, z + 0.005), finish, 0.0015),
        tube("ring_a", 0.019, 0.018, (x - length * 0.22, 0, tz), finish, 0.004),
        tube("ring_b", 0.019, 0.018, (x + length * 0.22, 0, tz), finish, 0.004),
        bx("ring_post_a", (0.018, 0.012, 0.024), (x - length * 0.22, 0, z + 0.018), finish, 0.0015),
        bx("ring_post_b", (0.018, 0.012, 0.024), (x + length * 0.22, 0, z + 0.018), finish, 0.0015),
        tube("scope_tube", 0.0152, length * 0.62, (x, 0, tz), finish, 0.0024),
        tube("scope_obj", 0.026, 0.075, (x + length * 0.5 - 0.0375, 0, tz), finish, 0.0026),
        cyl("scope_obj_taper", 0.021, 0.03, (x + length * 0.31 - 0.004, 0, tz), finish, 32),
        tube("scope_eye", 0.021, 0.07, (x - length * 0.5 + 0.035, 0, tz), finish, 0.0026),
        tube("scope_eye_ring", 0.0225, 0.022, (x - length * 0.37, 0, tz), RUBBER, 0.0025),
        cyl("turret_elev", 0.012, 0.022, (x, 0, tz + 0.024), finish, 24, 0.001, 'Z'),
        cyl("turret_wind", 0.012, 0.022, (x, -0.024, tz), finish, 24, 0.001, 'Y'),
        cyl("turret_saddle", 0.019, 0.034, (x, 0, tz), finish, 24, 0.002, 'Y'),
        cyl("parallax", 0.009, 0.016, (x, 0.022, tz), finish, 20, 0.001, 'Y'),
    ]


def ar_lower(finish, furniture):
    """AR-pattern lower: receiver, magwell, trigger guard, pistol grip,
    controls. Top of the pistol grip at the origin."""
    p = [
        profile("lower", [(-0.075, 0.0), (0.03, 0.0), (0.045, -0.02), (0.11, -0.02), (0.125, 0.0),
                          (0.13, 0.046), (-0.075, 0.046)], 0.024, finish, 0.0015),
        profile("magwell_flare", [(0.04, -0.018), (0.115, -0.018), (0.118, -0.036), (0.037, -0.036)], 0.027, finish, 0.0015),
        profile("trigger_guard", [(-0.005, -0.0), (0.045, -0.0), (0.05, -0.028), (-0.002, -0.03)], 0.004, finish, 0.001),
        bx("trigger", (0.004, 0.005, 0.022), (0.012, 0, -0.012), PARK, 0.001, (0, -15, 0)),
        profile("grip", [(-0.04, 0.0), (0.012, 0.0), (0.004, -0.03), (-0.008, -0.06), (-0.002, -0.08),
                         (-0.024, -0.112), (-0.06, -0.106), (-0.05, -0.05)], 0.03, furniture, 0.004),
        cyl("selector", 0.006, 0.03, (-0.025, 0, 0.022), PARK, 16, 0.001, 'Y'),
        bx("selector_lever", (0.016, 0.004, 0.006), (-0.02, 0.016, 0.026), PARK, 0.001),
        bx("bolt_catch", (0.018, 0.004, 0.012), (0.03, 0.0135, 0.026), PARK, 0.001),
        cyl("mag_release", 0.005, 0.03, (0.04, 0, 0.012), PARK, 16, 0.001, 'Y'),
        cyl("pin_front", 0.003, 0.027, (0.115, 0, 0.038), PARK, 12, 0.0, 'Y'),
        cyl("pin_rear", 0.003, 0.027, (-0.058, 0, 0.038), PARK, 12, 0.0, 'Y'),
    ]
    return p


def ar_upper(finish, x_end):
    """AR-pattern upper: receiver with ejection port, forward assist,
    charging handle and top rail, from the back of the lower to x_end."""
    upper = tc.box("upper", (x_end + 0.075, 0.023, 0.044), ((x_end - 0.075) / 2, 0, 0.068), 0.002, 2)
    cut(upper, [tc.box("port", (0.055, 0.008, 0.017), (0.045, -0.0115, 0.068), 0.001, 2)])
    p = [
        part(upper, finish),
        bx("port_cover", (0.05, 0.002, 0.006), (0.045, -0.0128, 0.08), finish, 0.0005),
        bx("bolt_face", (0.05, 0.002, 0.012), (0.045, -0.008, 0.068), PARK, 0.0005),
        bx("brass_deflector", (0.014, 0.012, 0.018), (0.012, -0.015, 0.078), finish, 0.002),
        cyl("forward_assist", 0.0065, 0.032, (-0.018, -0.017, 0.077), finish, 20, 0.001),
        cyl("fa_button", 0.0072, 0.006, (-0.035, -0.017, 0.077), PARK, 20, 0.001),
        bx("charging_handle", (0.02, 0.012, 0.008), (-0.084, 0, 0.083), finish, 0.001),
        bx("charging_latch", (0.012, 0.03, 0.006), (-0.088, 0, 0.083), finish, 0.0015),
    ]
    p += rail(-0.073, x_end, 0.1, finish)
    return p


# ---------------------------------------------------------------------------
# Weapons. Each returns (parts, muzzle point, sight point).
# ---------------------------------------------------------------------------

def carbine():
    hg0, hg1, cz = 0.16, 0.45, 0.07
    p = ar_lower(ANOD, POLY) + ar_upper(ANOD, hg0)
    p.append(octagon_handguard(hg0, hg1, cz, 0.024, ANOD))
    p += rail(hg0, hg1 - 0.005, 0.1, ANOD)
    p += [
        cyl("barrel_nut", 0.017, 0.012, (hg0 + 0.004, 0, cz), PARK, 24),
        cyl("barrel", 0.0082, 0.48, (0.32, 0, cz), PARK, 20),
        cyl("barrel_step", 0.0095, 0.03, (hg1 + 0.03, 0, cz), PARK, 20),
        flash_hider(0.57, cz, PARK),
        bx("front_sight_folded", (0.032, 0.016, 0.01), (hg1 - 0.03, 0, 0.105), POLY, 0.002),
        bx("rear_sight_folded", (0.03, 0.018, 0.01), (-0.055, 0, 0.105), POLY, 0.002),
        # Stock: buffer tube, adjustable stock with cheek riser and butt pad.
        cyl("buffer_tube", 0.0145, 0.19, (-0.165, 0, 0.068), ANOD, 24, 0.001),
        cyl("castle_nut", 0.017, 0.008, (-0.078, 0, 0.068), PARK, 12, 0.0005),
        profile("stock", [(-0.175, 0.088), (-0.3, 0.092), (-0.322, 0.088), (-0.33, 0.07), (-0.332, -0.02),
                          (-0.318, -0.028), (-0.29, -0.01), (-0.22, 0.035), (-0.175, 0.05)], 0.034, POLY, 0.004),
        bx("stock_web", (0.09, 0.036, 0.02), (-0.255, 0, 0.068), POLY, 0.004),
        bx("butt_pad", (0.012, 0.038, 0.12), (-0.336, 0, 0.03), RUBBER, 0.004),
        bx("stock_lever", (0.05, 0.012, 0.008), (-0.23, 0, 0.034), POLY, 0.002),
        cyl("sling_mount", 0.006, 0.044, (-0.3, 0, 0.0), PARK, 16, 0.001, 'Y'),
        bx("vertical_grip_rail", (0.04, 0.012, 0.004), (0.33, 0, cz - 0.026), ANOD, 0.0008),
        profile("angled_grip", [(0.3, cz - 0.028), (0.37, cz - 0.028), (0.36, cz - 0.05), (0.31, cz - 0.04)], 0.026, POLY, 0.004),
    ]
    p += magazine_curved(0.047, 0.108, -0.02, 0.15, 0.03, 0.022, FDE)
    p += red_dot(0.03, 0.1)
    return p, (0.6, 0, cz), (0.03, 0, 0.138)


def smg():
    """Roller-delayed style SMG: tubular receiver with a cocking tube,
    trigger housing, slim handguard, curved magazine, retractable stock."""
    cz = 0.06
    recv = tc.cylinder("receiver", 0.024, 0.27, (0.07, 0, cz), 'X', 32, 0.002)
    cut(recv, [tc.box("port", (0.04, 0.012, 0.016), (0.06, -0.02, cz + 0.004), 0.002, 2)])
    p = [
        part(recv, PARK),
        bx("receiver_spine", (0.27, 0.026, 0.012), (0.07, 0, cz - 0.02), PARK, 0.002),
        cyl("end_cap", 0.025, 0.012, (-0.068, 0, cz), PARK, 32, 0.002),
        cyl("cocking_tube", 0.0095, 0.2, (0.15, -0.012, cz + 0.024), PARK, 20, 0.001),
        bx("cocking_handle", (0.01, 0.026, 0.008), (0.23, -0.028, cz + 0.026), PARK, 0.002, (0, 0, -25)),
        tube("handguard", 0.026, 0.095, (0.25, 0, cz - 0.004), POLY, 0.006),
        bx("handguard_bottom", (0.095, 0.034, 0.016), (0.25, 0, cz - 0.026), POLY, 0.005),
        cyl("barrel", 0.0075, 0.11, (0.29, 0, cz), PARK, 20),
        flash_hider(0.355, cz, PARK, 0.0105, 0.035, 3),
        cyl("front_sight_post", 0.012, 0.02, (0.29, 0, cz + 0.026), PARK, 20, 0.001, 'Z'),
        tube("front_sight_hood", 0.011, 0.012, (0.29, 0, cz + 0.042), PARK, 0.002),
        tube("rear_drum", 0.01, 0.014, (-0.04, 0, cz + 0.038), PARK, 0.003),
        bx("rear_drum_base", (0.02, 0.02, 0.014), (-0.04, 0, cz + 0.026), PARK, 0.002),
        profile("trigger_housing", [(-0.06, 0.04), (0.06, 0.04), (0.065, 0.02), (0.05, 0.0), (0.0, -0.006), (-0.06, 0.004)], 0.03, POLY, 0.004),
        profile("trigger_guard", [(-0.004, 0.0), (0.046, 0.0), (0.05, -0.026), (0.0, -0.03)], 0.004, POLY, 0.001),
        bx("trigger", (0.004, 0.005, 0.02), (0.012, 0, -0.012), PARK, 0.001, (0, -15, 0)),
        profile("grip", [(-0.04, 0.004), (0.012, 0.004), (0.0, -0.06), (-0.004, -0.09), (-0.03, -0.1), (-0.055, -0.096), (-0.05, -0.04)], 0.03, POLY, 0.004),
        cyl("selector", 0.006, 0.034, (-0.03, 0, 0.02), PARK, 16, 0.001, 'Y'),
        bx("selector_lever", (0.018, 0.004, 0.006), (-0.024, 0.018, 0.024), PARK, 0.001),
        profile("magwell", [(0.07, 0.04), (0.11, 0.04), (0.112, 0.01), (0.068, 0.01)], 0.03, PARK, 0.002),
        bx("mag_release", (0.008, 0.032, 0.01), (0.115, 0, 0.026), PARK, 0.002),
        # Retractable stock: two rods and a butt plate.
        cyl("stock_rod_a", 0.0045, 0.17, (-0.15, 0.014, cz + 0.004), PARK, 12),
        cyl("stock_rod_b", 0.0045, 0.17, (-0.15, -0.014, cz + 0.004), PARK, 12),
        profile("butt_plate", [(-0.235, 0.075), (-0.245, 0.075), (-0.25, 0.0), (-0.245, -0.04), (-0.235, -0.04), (-0.232, 0.04)], 0.046, PARK, 0.003),
        bx("butt_rubber", (0.008, 0.046, 0.1), (-0.252, 0, 0.018), RUBBER, 0.003),
    ]
    p += magazine_curved(0.074, 0.104, 0.012, 0.19, 0.03, 0.022, PARK, True, "smg_mag")
    p += rail(-0.03, 0.12, cz + 0.042, PARK, 0.02)
    p += red_dot(0.03, cz + 0.042)
    return p, (0.375, 0, cz), (0.03, 0, cz + 0.08)


def marksman():
    hg0, hg1, cz = 0.18, 0.52, 0.074
    p = ar_lower(ANOD, ODG) + ar_upper(ANOD, hg0)
    p.append(octagon_handguard(hg0, hg1, cz, 0.027, ANOD, 5))
    p += rail(hg0, hg1 - 0.005, 0.1, ANOD)
    p += [
        cyl("barrel_nut", 0.019, 0.012, (hg0 + 0.004, 0, cz), PARK, 24),
        cyl("barrel", 0.0105, 0.6, (0.48, 0, cz), PARK, 24),
        cyl("brake_body", 0.0135, 0.06, (0.81, 0, cz), PARK, 24, 0.001),
        bx("bipod_mount", (0.03, 0.018, 0.01), (hg1 - 0.03, 0, cz - 0.032), ANOD, 0.0015),
        cyl("buffer_tube", 0.0145, 0.2, (-0.17, 0, 0.068), ANOD, 24, 0.001),
        profile("stock", [(-0.17, 0.098), (-0.36, 0.104), (-0.38, 0.08), (-0.385, -0.05), (-0.36, -0.06),
                          (-0.3, -0.03), (-0.25, -0.012), (-0.2, 0.025), (-0.17, 0.045)], 0.04, ODG, 0.004),
        bx("cheek_riser", (0.13, 0.034, 0.018), (-0.27, 0, 0.11), ODG, 0.005),
        cyl("riser_knob", 0.008, 0.012, (-0.27, -0.02, 0.1), PARK, 16, 0.001, 'Y'),
        bx("butt_pad", (0.015, 0.044, 0.155), (-0.39, 0, 0.022), RUBBER, 0.004),
        cyl("monopod", 0.007, 0.05, (-0.33, 0, -0.06), PARK, 16, 0.001, 'Z'),
    ]
    # Muzzle brake ports.
    brake = p[-10]
    cutters = [tc.box("brake_port", (0.01, 0.04, 0.012), (0.795 + i * 0.016, 0, cz), 0.002, 2) for i in range(3)]
    cut(brake, cutters)
    p += magazine_curved(0.05, 0.118, -0.02, 0.1, 0.006, 0.026, POLY, True, "mag308")
    p += scope(0.07, 0.1, 0.33)
    return p, (0.84, 0, cz), (0.07, 0, 0.142)


def pistol():
    slide = tc.box("slide", (0.19, 0.026, 0.032), (0.055, 0, 0.048), 0.003, 2)
    cutters = [tc.box("serration", (0.0025, 0.03, 0.026), (-0.03 + i * 0.006, 0, 0.05), 0.0, 1) for i in range(8)]
    cutters += [tc.box("serration_f", (0.0025, 0.03, 0.022), (0.105 + i * 0.006, 0, 0.05), 0.0, 1) for i in range(5)]
    cutters.append(tc.box("port", (0.035, 0.01, 0.014), (0.05, -0.012, 0.06), 0.001, 2))
    cut(slide, cutters)
    p = [
        part(slide, BLUED),
        bx("barrel_hood", (0.035, 0.016, 0.012), (0.05, -0.002, 0.058), PARK, 0.001),
        profile("frame", [(-0.04, 0.0), (0.15, 0.0), (0.15, 0.03), (-0.045, 0.034)], 0.024, POLY, 0.002),
        bx("accessory_rail", (0.04, 0.02, 0.008), (0.12, 0, -0.002), POLY, 0.001),
        profile("trigger_guard", [(0.0, 0.0), (0.06, 0.0), (0.065, -0.008), (0.05, -0.03), (0.0, -0.03)], 0.012, POLY, 0.002),
        bx("trigger", (0.004, 0.006, 0.02), (0.016, 0, -0.012), POLY, 0.001, (0, -12, 0)),
        profile("grip", [(-0.045, 0.004), (0.012, 0.004), (0.0, -0.04), (-0.008, -0.088), (-0.058, -0.088), (-0.06, -0.03)], 0.03, POLY, 0.004),
        bx("mag_base", (0.052, 0.028, 0.008), (-0.03, 0, -0.092), POLY, 0.003),
        cyl("barrel_tip", 0.0058, 0.008, (0.148, 0, 0.052), PARK, 20),
        bx("rear_sight", (0.008, 0.024, 0.008), (-0.032, 0, 0.068), BLUED, 0.001),
        bx("front_sight", (0.006, 0.005, 0.008), (0.135, 0, 0.068), BLUED, 0.001),
        cyl("front_dot", 0.0014, 0.002, (0.132, 0, 0.069), SIGHT_DOT, 12, 0.0),
        cyl("slide_stop", 0.004, 0.028, (0.045, 0, 0.028), PARK, 12, 0.0, 'Y'),
        cyl("takedown", 0.0035, 0.027, (0.08, 0, 0.022), PARK, 12, 0.0, 'Y'),
    ]
    tag(p, {"slide", "barrel_hood", "rear_sight", "front_sight", "front_dot"}, "slide")
    return p, (0.152, 0, 0.052), (0.05, 0, 0.074)


def shotgun():
    cz = 0.055
    recv = tc.box("receiver", (0.24, 0.036, 0.062), (0.05, 0, 0.04), 0.004, 2)
    cut(recv, [tc.box("port", (0.06, 0.01, 0.022), (0.06, -0.017, 0.05), 0.002, 2)])
    p = [
        part(recv, PARK),
        cyl("barrel", 0.0105, 0.52, (0.43, 0, cz), BLUED, 24),
        cyl("barrel_ring", 0.012, 0.008, (0.67, 0, cz), BLUED, 24, 0.001),
        cyl("mag_tube", 0.0115, 0.42, (0.38, 0, 0.023), BLUED, 24),
        cyl("mag_cap", 0.013, 0.025, (0.6, 0, 0.023), BLUED, 24, 0.002),
        bx("barrel_clamp", (0.016, 0.026, 0.05), (0.56, 0, 0.04), PARK, 0.002),
        cyl("bead", 0.0028, 0.006, (0.68, 0, cz + 0.013), BRASS, 12, 0.0, 'Z'),
        bx("ghost_ring", (0.012, 0.03, 0.022), (-0.04, 0, 0.081), PARK, 0.002),
        bx("rail_top", (0.12, 0.016, 0.006), (0.04, 0, 0.074), PARK, 0.001),
        profile("trigger_guard", [(-0.005, 0.01), (0.055, 0.01), (0.06, -0.028), (0.0, -0.032)], 0.012, PARK, 0.002),
        bx("trigger", (0.004, 0.006, 0.022), (0.016, 0, -0.008), PARK, 0.001, (0, -15, 0)),
        bx("safety", (0.006, 0.014, 0.01), (-0.01, 0, 0.074), PARK, 0.001),
        # Stock with pistol grip and butt pad.
        profile("stock", [(-0.07, 0.07), (-0.38, 0.06), (-0.4, 0.04), (-0.405, -0.075), (-0.38, -0.08),
                          (-0.24, -0.035), (-0.12, -0.005), (-0.06, -0.01)], 0.042, POLY, 0.005),
        profile("grip", [(-0.06, 0.0), (0.0, 0.0), (-0.01, -0.06), (-0.025, -0.095), (-0.06, -0.09), (-0.08, -0.03)], 0.032, POLY, 0.005),
        bx("butt_pad", (0.02, 0.046, 0.16), (-0.41, 0, -0.008), RUBBER, 0.005),
        cyl("sling_swivel", 0.005, 0.044, (-0.36, 0, -0.06), PARK, 12, 0.0, 'Y'),
    ]
    # Pump with grip grooves.
    pump = tc.cylinder("pump", 0.022, 0.17, (0.34, 0, 0.026), 'X', 32, 0.004)
    cut(pump, [tc.cylinder("pump_groove", 0.026, 0.006, (0.28 + i * 0.016, 0, 0.026), 'X', 32) if False else
               _groove(0.28 + i * 0.016, 0.026, 0.022) for i in range(8)])
    p.append(part(pump, POLY))
    return p, (0.69, 0, cz), (0.3, 0, 0.082)


def _groove(x, z, r):
    """Ring cutter for a circumferential groove at x (outer shell only)."""
    outer = tc.cylinder("groove_o", r + 0.01, 0.004, (x, 0, z), 'X', 32)
    inner = tc.cylinder("groove_i", r - 0.0018, 0.006, (x, 0, z), 'X', 32)
    cut(outer, [inner])
    return outer


WEAPONS = {
    "WeaponCarbine": carbine,
    "WeaponSMG": smg,
    "WeaponMarksman": marksman,
    "WeaponPistol": pistol,
    "WeaponShotgun": shotgun,
}


def worn_material(name):
    """Painted material with edge wear (curvature) and bevelled edges.

    tfps_col: base colour; tfps_mr: R metallic, G roughness, B wear. On worn
    parts, convex edges show lighter bare metal and lower roughness."""
    m = tc.painted_material(name, grain=0.025, bump_strength=0.035)
    nt = m.node_tree
    bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    mr = next(n for n in nt.nodes if n.type == 'ATTRIBUTE' and n.attribute_name == "tfps_mr")
    base_link = bsdf.inputs["Base Color"].links[0]
    base_src = base_link.from_socket
    rough_src = bsdf.inputs["Roughness"].links[0].from_socket
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    edge = tc.node(nt, "ShaderNodeMapRange")
    edge.inputs["From Min"].default_value = 0.52
    edge.inputs["From Max"].default_value = 0.6
    nt.links.new(geo.outputs["Pointiness"], edge.inputs["Value"])
    noise = tc.node(nt, "ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 180.0
    noise.inputs["Detail"].default_value = 4.0
    breakup = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY')
    nt.links.new(edge.outputs["Result"], breakup.inputs[0])
    nt.links.new(noise.outputs["Fac"], breakup.inputs[1])
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(mr.outputs["Color"], sep.inputs["Color"])
    amount = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY', use_clamp=True)
    nt.links.new(breakup.outputs["Value"], amount.inputs[0])
    amount2 = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY', use_clamp=True)
    nt.links.new(amount.outputs["Value"], amount2.inputs[0])
    amount2.inputs[1].default_value = 2.2
    nt.links.new(sep.outputs["Blue"], amount.inputs[1])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = 'RGBA'
    nt.links.new(amount2.outputs["Value"], mix.inputs["Factor"])
    nt.links.new(base_src, mix.inputs[6])
    mix.inputs[7].default_value = (0.22, 0.22, 0.21, 1.0)
    nt.links.new(mix.outputs[2], bsdf.inputs["Base Color"])
    rmix = tc.node(nt, "ShaderNodeMapRange")
    nt.links.new(amount2.outputs["Value"], rmix.inputs["Value"])
    nt.links.new(rough_src, rmix.inputs["To Min"])
    rmix.inputs["To Max"].default_value = 0.3
    nt.links.new(rmix.outputs["Result"], bsdf.inputs["Roughness"])
    # Rounded edges in the normal bake.
    bump_link = bsdf.inputs["Normal"].links[0]
    bump = bump_link.from_node
    bevel = nt.nodes.new("ShaderNodeBevel")
    bevel.samples = 8
    bevel.inputs["Radius"].default_value = 0.0012
    nt.links.new(bevel.outputs["Normal"], bump.inputs["Normal"])
    return m


_render_preview = tc.render_preview
tc.render_preview = lambda o, path, distance_scale=1.45: _render_preview(o, path, distance_scale)


def build_one(asset, fn):
    tc.reset_scene()
    parts, muzzle, sight = fn()
    # AR-pattern charging handle: its own mesh (pulled on empty reloads).
    tag(parts, {"charging_handle", "charging_latch"}, "charge")
    gun = tc.join(parts, asset)
    # Bake the transform in, then turn the barrel from +X to +Y.
    gun.data.transform(gun.matrix_world)
    gun.matrix_world = Matrix.Identity(4)
    turn = Matrix.Rotation(math.radians(90.0), 4, 'Z')
    gun.data.transform(turn)
    grip = turn @ Vector((0.0, 0.0, 0.0))
    muzzle_v = turn @ Vector(muzzle)
    sight_v = turn @ Vector(sight)
    vs = [v.co for v in gun.data.vertices]
    mn = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    mx = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    offset = Vector(((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, mn.z))
    tc.set_origin_floor_center(gun)
    grip -= offset
    muzzle_v -= offset
    sight_v -= offset
    tc.shade_auto_smooth(gun, 35.0)
    tc.uv_unwrap(gun, 0.003)
    mat = worn_material(f"M_{asset}_Proc")
    gun.data.materials.append(mat)
    lod1 = tc.make_lod(gun, 0.4, f"{asset}_LOD1")
    manifest = tc.finish_asset(asset, gun, lod1, [], mat,
                               notes="Weapon model; barrel along +Y (Godot -Z). grip_m / muzzle_m / sight_m in model space (Blender axes); the magazine is the separate mesh SM_<asset>_Mag (mag_m: its top centre).",
                               extra={"grip_m": [round(c, 4) for c in grip], "muzzle_m": [round(c, 4) for c in muzzle_v],
                                      "sight_m": [round(c, 4) for c in sight_v]})
    split_magazine(asset, gun, manifest)
    return manifest


def split_magazine(asset, gun, manifest):
    """Moves the faces of vertex groups 'mag' (the magazine) and 'slide'
    (a pistol's slide and sights, an AR charging handle) into their own objects (same baked
    material and UVs) and re-exports the FBX with all of them, so the game
    can animate them (reloads, the slide cycling on each shot)."""
    import json
    parts = [gun]
    for group, suffix, key in (("mag", "Mag", "mag_m"), ("slide", "Slide", "slide_m"), ("charge", "Charge", "charge_m")):
        g = gun.vertex_groups.get(group)
        if g is None:
            continue
        gi = g.index
        sel = {v.index for v in gun.data.vertices if any(e.group == gi and e.weight > 0.5 for e in v.groups)}
        if not sel:
            continue
        tc.select_only([gun])
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='DESELECT')
        bpy.ops.object.mode_set(mode='OBJECT')
        for v in gun.data.vertices:
            v.select = v.index in sel
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_mode(type='VERT')
        bpy.ops.mesh.separate(type='SELECTED')
        bpy.ops.object.mode_set(mode='OBJECT')
        piece = [o for o in bpy.context.selected_objects if o is not gun][0]
        piece.name = f"SM_{asset}_{suffix}"
        vs = [piece.matrix_world @ v.co for v in piece.data.vertices]
        manifest[key] = [round((min(v.x for v in vs) + max(v.x for v in vs)) / 2, 4),
                         round((min(v.y for v in vs) + max(v.y for v in vs)) / 2, 4),
                         round(max(v.z for v in vs), 4)]
        parts.append(piece)
    if len(parts) == 1:
        return
    path = os.path.join(tc.REPO, manifest["files"]["fbx_lod0"])
    tc.export_fbx(parts, path)
    manifest["triangles_lod0"] = sum(tc.triangle_count(o) for o in parts)
    out = os.path.join(tc.REPO, "Assets", "Exports", asset, "manifest.json")
    with open(out, "w", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(tc.REPO, manifest["files"]["blend"]), compress=True)


def tag(parts, names, group):
    """Adds the parts whose object name is in `names` to vertex group `group`."""
    for o in parts:
        if o.name.split(".")[0] in names:
            g = o.vertex_groups.get(group) or o.vertex_groups.new(name=group)
            g.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')


def build():
    only = os.environ.get("TFPS_WEAPON")
    for asset, fn in WEAPONS.items():
        if only and only != asset:
            continue
        build_one(asset, fn)


if __name__ == "__main__":
    build()
