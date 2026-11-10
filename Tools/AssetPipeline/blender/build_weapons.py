"""Weapons: carbine (AR-7), SMG (K-9), marksman rifle (M-308), pistol (P-9)
and pump shotgun (SG-12). Original designs built from bevelled extruded
silhouettes and cylinders.

Built along +X (barrel forward) with Z up, then turned so the barrel points
along +Y: Godot imports Blender +Y as -Z, its forward axis. The manifest
records the grip (the top of the pistol grip, where the hand holds it) and
the muzzle, in the exported model's coordinates (metres, Blender axes), so
the game can hold the weapon and place muzzle flashes.

blender -b --factory-startup --python Tools/AssetPipeline/blender/build_weapons.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import tfps_common as tc  # noqa: E402

STEEL = ((0.07, 0.072, 0.075), 0.75, 0.38)
STEEL_WORN = ((0.16, 0.16, 0.165), 0.85, 0.3)
POLY = ((0.11, 0.11, 0.105), 0.0, 0.75)
TAN = ((0.24, 0.2, 0.135), 0.0, 0.8)
OD = ((0.13, 0.145, 0.095), 0.0, 0.78)


def part(o, finish):
    rgb, metallic, rough = finish
    return tc.paint(o, rgb, metallic, rough)


def profile(name, pts, thickness, finish, bevel=0.003):
    return part(tc.extrude_profile(name, pts, thickness, bevel), finish)


def bx(name, size, centre, finish, bevel=0.003):
    return part(tc.box(name, size, centre, bevel), finish)


def cyl(name, r, length, centre, finish, verts=20, bevel=0.0):
    return part(tc.cylinder(name, r, length, centre, 'X', verts, bevel), finish)


def tube(name, r, length, centre, finish, wall=0.0025, verts=24):
    """Open tube along X (no end caps), so the eye can look through an optic."""
    o = tc.cylinder(name, r, length, centre, 'X', verts)
    import bmesh
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


# ---------------------------------------------------------------------------
# Common pieces (x forward, z up; the top of the pistol grip is at the origin)
# ---------------------------------------------------------------------------

def pistol_grip(finish=POLY, angle=0.0):
    a = angle
    return profile("grip", [(-0.036, 0.0), (0.012, 0.0), (-0.006 - a, -0.105), (-0.054 - a, -0.1)], 0.03, finish, 0.004)


def trigger_group(x=0.0):
    return [
        bx("guard_bottom", (0.07, 0.008, 0.006), (x + 0.03, 0, -0.026), STEEL_WORN, 0.001),
        bx("guard_front", (0.006, 0.008, 0.028), (x + 0.063, 0, -0.013), STEEL_WORN, 0.001),
        part(tc.box("trigger", (0.006, 0.006, 0.02), (x + 0.03, 0, -0.012), 0.001), STEEL),
    ]


def red_dot(x, z):
    return [
        bx("optic_mount", (0.045, 0.03, 0.016), (x, 0, z), STEEL, 0.002),
        tube("optic_body", 0.021, 0.07, (x, 0, z + 0.03), STEEL),
        tube("optic_hood", 0.023, 0.012, (x + 0.032, 0, z + 0.03), STEEL),
    ]


def scope(x, z, length=0.30):
    return [
        bx("scope_ring_a", (0.02, 0.03, 0.03), (x - length * 0.25, 0, z), STEEL, 0.002),
        bx("scope_ring_b", (0.02, 0.03, 0.03), (x + length * 0.25, 0, z), STEEL, 0.002),
        tube("scope_tube", 0.017, length, (x, 0, z + 0.03), STEEL),
        tube("scope_bell_obj", 0.026, 0.07, (x + length * 0.5 - 0.02, 0, z + 0.03), STEEL, 0.003),
        tube("scope_bell_eye", 0.022, 0.05, (x - length * 0.5 + 0.02, 0, z + 0.03), STEEL, 0.003),
        part(tc.cylinder("scope_turret", 0.012, 0.022, (x, 0, z + 0.058), 'Z', 16), STEEL),
    ]


# ---------------------------------------------------------------------------
# Weapons. Each returns (parts, muzzle point, sight point: the centre of
# the optic or the line over the iron sights, where the eye looks along).
# ---------------------------------------------------------------------------

def carbine():
    p = [
        profile("lower", [(-0.06, 0.0), (0.13, 0.0), (0.15, 0.025), (0.15, 0.056), (-0.06, 0.056)], 0.03, STEEL),
        bx("upper", (0.27, 0.034, 0.04), (0.055, 0, 0.075), STEEL),
        bx("rail", (0.42, 0.022, 0.012), (0.13, 0, 0.101), STEEL_WORN, 0.001),
        bx("handguard", (0.28, 0.048, 0.062), (0.33, 0, 0.072), TAN, 0.008),
        cyl("barrel", 0.0085, 0.17, (0.55, 0, 0.072), STEEL),
        cyl("muzzle_device", 0.013, 0.055, (0.655, 0, 0.072), STEEL, 6, 0.002),
        bx("front_sight", (0.012, 0.012, 0.03), (0.44, 0, 0.115), STEEL),
        bx("mag_well", (0.08, 0.034, 0.03), (0.075, 0, 0.012), STEEL),
        profile("magazine", [(0.045, 0.0), (0.105, 0.0), (0.125, -0.16), (0.068, -0.17)], 0.026, POLY, 0.003),
        cyl("buffer_tube", 0.015, 0.17, (-0.14, 0, 0.075), STEEL),
        profile("stock", [(-0.13, 0.098), (-0.33, 0.098), (-0.345, 0.0), (-0.32, -0.03), (-0.2, 0.01), (-0.13, 0.045)], 0.04, TAN, 0.006),
        bx("charging_handle", (0.03, 0.04, 0.012), (-0.075, 0, 0.088), STEEL_WORN, 0.002),
        bx("ejection_port", (0.06, 0.004, 0.018), (0.075, 0.017, 0.075), STEEL_WORN, 0.001),
        pistol_grip(TAN),
    ] + trigger_group() + red_dot(0.05, 0.115)
    return p, (0.685, 0, 0.072), (0.05, 0, 0.145)


def smg():
    p = [
        bx("receiver", (0.30, 0.04, 0.085), (0.07, 0, 0.042), POLY, 0.008),
        bx("top_rail", (0.22, 0.022, 0.012), (0.06, 0, 0.09), STEEL_WORN, 0.001),
        cyl("barrel", 0.0095, 0.09, (0.26, 0, 0.055), STEEL),
        cyl("muzzle_nut", 0.014, 0.03, (0.3, 0, 0.055), STEEL, 6, 0.002),
        profile("magazine", [(0.06, 0.0), (0.095, 0.0), (0.1, -0.2), (0.068, -0.2)], 0.024, STEEL),
        profile("foregrip", [(0.17, 0.0), (0.2, 0.0), (0.195, -0.09), (0.172, -0.09)], 0.03, POLY, 0.005),
        cyl("stock_rod_a", 0.006, 0.22, (-0.19, 0.012, 0.07), STEEL),
        cyl("stock_rod_b", 0.006, 0.22, (-0.19, -0.012, 0.07), STEEL),
        bx("butt_plate", (0.02, 0.04, 0.11), (-0.3, 0, 0.04), POLY, 0.004),
        bx("cocking_tube", (0.12, 0.012, 0.016), (0.12, 0.024, 0.07), STEEL_WORN, 0.002),
        pistol_grip(POLY),
    ] + trigger_group() + red_dot(0.04, 0.104)
    return p, (0.315, 0, 0.055), (0.04, 0, 0.134)


def marksman():
    p = [
        profile("lower", [(-0.07, 0.0), (0.15, 0.0), (0.17, 0.03), (0.17, 0.06), (-0.07, 0.06)], 0.034, STEEL),
        bx("upper", (0.32, 0.038, 0.045), (0.07, 0, 0.082), STEEL),
        bx("rail", (0.5, 0.022, 0.012), (0.17, 0, 0.11), STEEL_WORN, 0.001),
        bx("handguard", (0.36, 0.052, 0.066), (0.41, 0, 0.078), OD, 0.008),
        cyl("barrel", 0.011, 0.3, (0.73, 0, 0.078), STEEL),
        cyl("brake", 0.015, 0.07, (0.9, 0, 0.078), STEEL, 8, 0.002),
        bx("mag_well", (0.1, 0.038, 0.03), (0.085, 0, 0.014), STEEL),
        profile("magazine", [(0.05, 0.0), (0.12, 0.0), (0.125, -0.1), (0.055, -0.105)], 0.03, POLY, 0.003),
        profile("stock", [(-0.07, 0.1), (-0.38, 0.11), (-0.4, -0.01), (-0.36, -0.045), (-0.22, -0.01), (-0.12, -0.02), (-0.07, 0.03)], 0.042, OD, 0.006),
        bx("cheek_rest", (0.14, 0.036, 0.02), (-0.25, 0, 0.12), POLY, 0.005),
        pistol_grip(OD),
    ] + trigger_group() + scope(0.1, 0.12)
    return p, (0.94, 0, 0.078), (0.1, 0, 0.15)


def pistol():
    p = [
        bx("slide", (0.19, 0.03, 0.034), (0.055, 0, 0.05), STEEL, 0.004),
        bx("frame", (0.16, 0.028, 0.03), (0.045, 0, 0.018), POLY, 0.004),
        cyl("barrel_tip", 0.0065, 0.012, (0.152, 0, 0.052), STEEL_WORN),
        bx("rear_sight", (0.01, 0.026, 0.008), (-0.03, 0, 0.071), STEEL),
        bx("front_sight", (0.008, 0.008, 0.008), (0.14, 0, 0.071), STEEL),
        bx("serrations", (0.03, 0.032, 0.026), (-0.015, 0, 0.05), STEEL_WORN, 0.002),
        profile("grip", [(-0.04, 0.005), (0.015, 0.005), (-0.012, -0.09), (-0.06, -0.085)], 0.031, POLY, 0.005),
    ] + trigger_group(-0.005)
    return p, (0.16, 0, 0.052), (0.05, 0, 0.076)


def shotgun():
    p = [
        bx("receiver", (0.22, 0.04, 0.07), (0.05, 0, 0.04), STEEL, 0.006),
        cyl("barrel", 0.011, 0.5, (0.41, 0, 0.065), STEEL),
        cyl("mag_tube", 0.012, 0.4, (0.36, 0, 0.035), STEEL_WORN),
        part(tc.cylinder("pump", 0.021, 0.17, (0.33, 0, 0.038), 'X', 20, 0.004), POLY),
        part(tc.cylinder("bead", 0.004, 0.008, (0.65, 0, 0.08), 'Z', 8), (( 0.9, 0.9, 0.85), 0.5, 0.3)),
        profile("stock", [(-0.06, 0.07), (-0.38, 0.06), (-0.4, -0.06), (-0.36, -0.07), (-0.12, -0.01), (-0.06, 0.0)], 0.042, POLY, 0.006),
        bx("butt_pad", (0.02, 0.044, 0.13), (-0.405, 0, 0.0), ((0.05, 0.05, 0.05), 0.0, 0.9), 0.006),
        pistol_grip(POLY),
    ] + trigger_group()
    return p, (0.66, 0, 0.065), (0.3, 0, 0.084)


WEAPONS = {
    "WeaponCarbine": carbine,
    "WeaponSMG": smg,
    "WeaponMarksman": marksman,
    "WeaponPistol": pistol,
    "WeaponShotgun": shotgun,
}


def build_one(asset, fn):
    tc.reset_scene()
    parts, muzzle, sight = fn()
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
    tc.shade_auto_smooth(gun, 40.0)
    tc.uv_unwrap(gun)
    mat = tc.painted_material(f"M_{asset}_Proc", grain=0.05, bump_strength=0.08)
    gun.data.materials.append(mat)
    lod1 = tc.make_lod(gun, 0.45, f"{asset}_LOD1")
    return tc.finish_asset(asset, gun, lod1, [], mat,
                           notes="Weapon model; barrel along +Y (Godot -Z). grip_m / muzzle_m / sight_m in model space (Blender axes).",
                           extra={"grip_m": [round(c, 4) for c in grip], "muzzle_m": [round(c, 4) for c in muzzle_v],
                                  "sight_m": [round(c, 4) for c in sight_v]})


def build():
    only = os.environ.get("TFPS_WEAPON")
    for asset, fn in WEAPONS.items():
        if only and only != asset:
            continue
        build_one(asset, fn)


if __name__ == "__main__":
    build()
