"""Operator character in body parts: torso, head, thigh, shin, upper arm and
forearm, for each team (A: attackers, coyote gear on olive; B: defenders,
black gear on slate). Original design: uniform, plate carrier with pouches,
cut helmet with ear protection and goggles, knee pads, boots and gloves.

Each part is modelled around its joint (the hip for the torso and thigh,
the knee for the shin, the neck for the head, the shoulder and elbow for the
arm), facing Blender +Y (Godot -Z), limbs hanging along -Z. The game turns
the parts at those joints; the manifest records the joint as `joint_m` in
the exported model's space (metres, Blender axes). Limbs are built for the
right side; the game mirrors them for the left.

blender -b --factory-startup --python Tools/AssetPipeline/blender/build_operator.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import tfps_common as tc  # noqa: E402

TEAMS = {
    "A": {
        "uniform": ((0.115, 0.125, 0.07), 0.0, 0.92),
        "gear": ((0.21, 0.155, 0.09), 0.0, 0.85),
        "webbing": ((0.14, 0.1, 0.06), 0.0, 0.88),
        "helmet": ((0.16, 0.14, 0.09), 0.0, 0.7),
        "accent": ((0.9, 0.3, 0.03), 0.0, 0.6),
    },
    "B": {
        "uniform": ((0.05, 0.06, 0.08), 0.0, 0.92),
        "gear": ((0.035, 0.035, 0.035), 0.0, 0.85),
        "webbing": ((0.02, 0.02, 0.02), 0.0, 0.88),
        "helmet": ((0.045, 0.05, 0.06), 0.0, 0.65),
        "accent": ((0.05, 0.25, 0.9), 0.0, 0.6),
    },
}
SKIN = ((0.42, 0.27, 0.19), 0.0, 0.6)
BOOT = ((0.06, 0.045, 0.03), 0.0, 0.6)
RUBBER = ((0.02, 0.02, 0.02), 0.0, 0.9)
GLOVE = ((0.04, 0.04, 0.036), 0.0, 0.75)
LENS = ((0.02, 0.03, 0.035), 0.4, 0.08)
METAL = ((0.08, 0.08, 0.085), 0.8, 0.4)


def part(o, finish):
    rgb, metallic, rough = finish
    return tc.paint(o, rgb, metallic, rough)


def soft(name, size, centre, finish, round_=0.35, segments=3):
    """Rounded box: bevelled with a large radius (cloth, padding, limbs)."""
    return part(tc.box(name, size, centre, min(size) * round_, segments), finish)


def bx(name, size, centre, finish, bevel=0.004):
    return part(tc.box(name, size, centre, bevel), finish)


def ball(name, radii, centre, finish, segments=24, rings=14):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=rings, radius=1.0, location=centre)
    o = bpy.context.active_object
    o.name = name
    o.scale = radii
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return part(o, finish)


def cyl(name, r, length, centre, finish, axis='Z', verts=16, bevel=0.0):
    return part(tc.cylinder(name, r, length, centre, axis, verts, bevel), finish)


def shell(name, radii, centre, cut_z, thickness, finish):
    """Dome: the top of an ellipsoid above `cut_z`, with a wall thickness."""
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=18, radius=1.0, location=(0, 0, 0))
    o = bpy.context.active_object
    o.name = name
    o.scale = radii
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    o.location = centre
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
    bmesh.ops.bisect_plane(bm, geom=geom, plane_co=(0, 0, cut_z), plane_no=(0, 0, 1), clear_inner=True)
    bm.to_mesh(o.data)
    bm.free()
    m = o.modifiers.new("Solidify", 'SOLIDIFY')
    m.thickness = thickness
    m.offset = -1.0
    tc.apply_modifiers(o)
    return part(o, finish)


# ---------------------------------------------------------------------------
# Parts. Each returns its list of objects; the joint is at the origin.
# ---------------------------------------------------------------------------

def torso(t):
    u, g, w, a = t["uniform"], t["gear"], t["webbing"], t["accent"]
    p = [
        soft("pelvis", (0.33, 0.21, 0.17), (0, 0, 0.05), u),
        bx("belt", (0.35, 0.23, 0.05), (0, 0, 0.13), w, 0.01),
        bx("buckle", (0.06, 0.02, 0.035), (0, 0.118, 0.13), METAL, 0.003),
        bx("hip_pouch_r", (0.06, 0.09, 0.1), (0.17, -0.02, 0.1), g, 0.012),
        bx("hip_pouch_l", (0.05, 0.08, 0.09), (-0.17, -0.03, 0.1), g, 0.012),
        bx("dump_pouch", (0.14, 0.05, 0.1), (0, -0.125, 0.09), g, 0.015),
        soft("abdomen", (0.3, 0.19, 0.2), (0, 0, 0.25), u),
        soft("chest", (0.39, 0.23, 0.25), (0, 0, 0.43), u),
        soft("shoulders", (0.45, 0.19, 0.11), (0, 0, 0.53), u, 0.45),
        soft("collar", (0.17, 0.13, 0.05), (0, 0, 0.585), u, 0.45),
        # Plate carrier: front and back plates, cummerbund, shoulder straps.
        bx("plate_front", (0.31, 0.05, 0.31), (0, 0.115, 0.4), g, 0.02),
        bx("plate_back", (0.31, 0.05, 0.33), (0, -0.115, 0.41), g, 0.02),
        bx("cummerbund_r", (0.03, 0.2, 0.13), (0.175, 0, 0.33), w, 0.01),
        bx("cummerbund_l", (0.03, 0.2, 0.13), (-0.175, 0, 0.33), w, 0.01),
        bx("strap_r", (0.065, 0.25, 0.025), (0.1, 0, 0.555), w, 0.008),
        bx("strap_l", (0.065, 0.25, 0.025), (-0.1, 0, 0.555), w, 0.008),
        bx("admin_pouch", (0.16, 0.035, 0.1), (0, 0.155, 0.5), g, 0.012),
        bx("patch", (0.07, 0.006, 0.04), (0.045, 0.176, 0.52), a, 0.002),
        bx("radio", (0.07, 0.05, 0.14), (0.08, -0.16, 0.44), g, 0.012),
        cyl("antenna", 0.006, 0.22, (0.1, -0.165, 0.6), RUBBER),
        bx("hydration", (0.16, 0.04, 0.2), (-0.03, -0.155, 0.36), g, 0.02),
        soft("neck", (0.1, 0.1, 0.08), (0, 0.005, 0.6), SKIN, 0.45),
    ]
    for i, x in enumerate((-0.09, 0.0, 0.09)):
        p.append(bx(f"mag_pouch{i}", (0.075, 0.05, 0.12), (x, 0.162, 0.33), g, 0.01))
        p.append(bx(f"mag_flap{i}", (0.077, 0.054, 0.025), (x, 0.163, 0.395), w, 0.006))
    return p


def head(t):
    g, w = t["gear"], t["webbing"]
    return [
        soft("neck_lower", (0.09, 0.09, 0.08), (0, 0, 0.03), SKIN, 0.45),
        ball("skull", (0.09, 0.105, 0.115), (0, 0.005, 0.15), SKIN),
        bx("nose", (0.022, 0.03, 0.04), (0, 0.105, 0.14), SKIN, 0.008),
        ball("brow", (0.085, 0.04, 0.02), (0, 0.08, 0.175), SKIN, 16, 8),
        # Face cover (lower half) and goggles.
        soft("face_cover", (0.17, 0.15, 0.09), (0, 0.025, 0.075), w, 0.4),
        bx("goggle_frame", (0.18, 0.035, 0.055), (0, 0.098, 0.165), RUBBER, 0.012),
        bx("goggle_lens", (0.16, 0.012, 0.042), (0, 0.115, 0.165), LENS, 0.006),
        bx("goggle_strap", (0.2, 0.18, 0.022), (0, -0.005, 0.17), RUBBER, 0.006),
        # Helmet: cut shell, rails, mount and cover band.
        shell("helmet", (0.128, 0.142, 0.12), (0, -0.005, 0.17), 0.155, 0.012, t["helmet"]),
        bx("rail_r", (0.012, 0.15, 0.025), (0.128, -0.005, 0.2), METAL, 0.003),
        bx("rail_l", (0.012, 0.15, 0.025), (-0.128, -0.005, 0.2), METAL, 0.003),
        bx("nvg_mount", (0.05, 0.025, 0.045), (0, 0.132, 0.225), METAL, 0.006),
        bx("counterweight", (0.08, 0.03, 0.05), (0, -0.145, 0.205), g, 0.01),
        bx("helmet_patch", (0.04, 0.03, 0.006), (0, -0.04, 0.293), t["accent"], 0.002),
        # Ear protection with the headband over the helmet.
        cyl("ear_r", 0.045, 0.045, (0.112, 0.0, 0.145), g, 'X', 20, 0.006),
        cyl("ear_l", 0.045, 0.045, (-0.112, 0.0, 0.145), g, 'X', 20, 0.006),
        cyl("mic", 0.004, 0.09, (0.08, 0.06, 0.1), RUBBER, 'Y'),
    ]


def thigh(t):
    u, g, w = t["uniform"], t["gear"], t["webbing"]
    return [
        soft("thigh", (0.155, 0.165, 0.5), (0, 0, -0.22), u, 0.45),
        bx("leg_pouch", (0.045, 0.1, 0.13), (0.085, 0.005, -0.21), g, 0.012),
        bx("leg_strap", (0.165, 0.175, 0.02), (0, 0, -0.17), w, 0.006),
        bx("cargo_seam", (0.004, 0.06, 0.18), (0.079, 0.0, -0.3), w, 0.001),
    ]


def shin(t):
    u, g = t["uniform"], t["gear"]
    return [
        soft("shin", (0.125, 0.135, 0.42), (0, 0, -0.2), u, 0.45),
        soft("knee_pad", (0.12, 0.06, 0.13), (0, 0.065, -0.03), g, 0.4),
        bx("knee_strap", (0.13, 0.14, 0.02), (0, 0, -0.06), RUBBER, 0.006),
        soft("boot", (0.12, 0.25, 0.13), (0, 0.04, -0.42), BOOT, 0.3),
        soft("boot_top", (0.125, 0.14, 0.09), (0, 0, -0.35), BOOT, 0.35),
        bx("sole", (0.122, 0.26, 0.03), (0, 0.04, -0.475), RUBBER, 0.008),
        bx("laces", (0.05, 0.08, 0.004), (0, 0.11, -0.36), RUBBER, 0.001),
    ]


def upper_arm(t):
    u, a = t["uniform"], t["accent"]
    return [
        soft("upper_arm", (0.105, 0.11, 0.33), (0, 0, -0.14), u, 0.45),
        soft("shoulder_cap", (0.12, 0.13, 0.08), (0, 0, -0.01), u, 0.45),
        bx("arm_patch", (0.006, 0.06, 0.07), (0.054, 0, -0.08), a, 0.002),
    ]


def forearm(t):
    u = t["uniform"]
    return [
        soft("forearm", (0.088, 0.092, 0.25), (0, 0, -0.11), u, 0.45),
        soft("cuff", (0.09, 0.094, 0.03), (0, 0, -0.225), u, 0.4),
        soft("glove", (0.07, 0.095, 0.1), (0, 0.005, -0.29), GLOVE, 0.35),
        soft("thumb", (0.03, 0.03, 0.06), (-0.03, 0.04, -0.28), GLOVE, 0.4),
        bx("knuckles", (0.072, 0.03, 0.035), (0, 0.04, -0.3), RUBBER, 0.008),
    ]


PARTS = {
    "Torso": torso,
    "Head": head,
    "Thigh": thigh,
    "Shin": shin,
    "UpperArm": upper_arm,
    "Forearm": forearm,
}


def build_one(asset, objs):
    gun = tc.join(objs, asset)
    gun.data.transform(gun.matrix_world)
    gun.matrix_world = Matrix.Identity(4)
    vs = [v.co for v in gun.data.vertices]
    mn = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    mx = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    offset = Vector(((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, mn.z))
    tc.set_origin_floor_center(gun)
    joint = Vector((0, 0, 0)) - offset
    tc.shade_auto_smooth(gun, 50.0)
    tc.uv_unwrap(gun)
    mat = tc.painted_material(f"M_{asset}_Proc", grain=0.06, bump_strength=0.12)
    gun.data.materials.append(mat)
    lod1 = tc.make_lod(gun, 0.5, f"{asset}_LOD1")
    return tc.finish_asset(asset, gun, lod1, [], mat,
                           notes="Operator body part; faces +Y (Godot -Z), limbs along -Z. joint_m in model space (Blender axes).",
                           extra={"joint_m": [round(c, 4) for c in joint]})


def build():
    only = os.environ.get("TFPS_PART")
    for team, colours in TEAMS.items():
        for name, fn in PARTS.items():
            asset = f"Operator{team}{name}"
            if only and only not in (asset, name):
                continue
            tc.reset_scene()
            build_one(asset, fn(colours))


if __name__ == "__main__":
    build()
