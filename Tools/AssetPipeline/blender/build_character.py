"""Operator character: one continuous skinned body with clothing, a plate
carrier, helmet and gear, rigged to a skeleton, for each team.

The body is grown from a skeleton graph with Blender's Skin modifier and
subdivided, so the figure is one smooth surface (no separate blocks). The
clothing is the body itself, painted by region (shirt, trousers, gloves,
boots, balaclava); the plate carrier, belt, knee pads and boot uppers are
shells extracted from the body surface and thickened, so they follow its
shape. Pouches, helmet, goggles and ear protection are modelled parts bound
rigidly to one bone each. Body weights come from distances to the bones.

Faces Blender +Y (Godot -Z), stands on Z = 0, about 1.8 m tall; A-pose.
Bones: hips, spine, chest, neck, head, clavicle/upper_arm/forearm/hand (.L
/.R), thigh/shin/foot/toe (.L/.R). The game poses them procedurally (walk
cycle, crouch, lean, aim) and solves arms and legs with two-bone IK.

Outputs Assets/Exports/Operator{A,B}/SM_Operator{A,B}.fbx (mesh + armature)
and 2048 px textures.

blender -b --factory-startup --python Tools/AssetPipeline/blender/build_character.py
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
        "shirt": ((0.105, 0.115, 0.065), 0.0, 0.93),
        "pants": ((0.13, 0.12, 0.075), 0.0, 0.93),
        "gear": ((0.2, 0.15, 0.088), 0.0, 0.86),
        "webbing": ((0.13, 0.095, 0.058), 0.0, 0.9),
        "helmet": ((0.15, 0.13, 0.085), 0.0, 0.72),
        "accent": ((0.85, 0.28, 0.03), 0.0, 0.6),
    },
    "B": {
        "shirt": ((0.045, 0.052, 0.068), 0.0, 0.93),
        "pants": ((0.035, 0.04, 0.052), 0.0, 0.93),
        "gear": ((0.03, 0.03, 0.032), 0.0, 0.86),
        "webbing": ((0.018, 0.018, 0.02), 0.0, 0.9),
        "helmet": ((0.04, 0.045, 0.052), 0.0, 0.68),
        "accent": ((0.05, 0.22, 0.85), 0.0, 0.6),
    },
}
SKIN = ((0.4, 0.26, 0.19), 0.0, 0.55)
BALACLAVA = ((0.03, 0.03, 0.03), 0.0, 0.95)
BOOT = ((0.07, 0.052, 0.035), 0.0, 0.62)
RUBBER = ((0.018, 0.018, 0.018), 0.0, 0.88)
GLOVE = ((0.035, 0.035, 0.032), 0.0, 0.7)
# First-person gloves: coyote for team A, wolf grey for team B (read against black weapons).
GLOVES = {"A": ((0.15, 0.1, 0.06), 0.0, 0.78), "B": ((0.085, 0.085, 0.08), 0.0, 0.8)}
LENS = ((0.015, 0.02, 0.025), 0.5, 0.06)
METAL = ((0.07, 0.07, 0.075), 0.8, 0.42)

# ---------------------------------------------------------------------------
# Skeleton (A-pose). The character faces +Y, so its right side is +X:
# ".R" bones are on +X, ".L" on -X.
# ---------------------------------------------------------------------------

def _side(p, s):
    return Vector((p[0] * s, p[1], p[2]))


JOINTS = {
    "hips": (0, 0.0, 0.97),
    "spine": (0, -0.005, 1.1),
    "chest": (0, -0.01, 1.27),
    "neck": (0, 0.0, 1.47),
    "head": (0, 0.01, 1.56),
    "head_top": (0, 0.01, 1.8),
}
LIMB = {  # right side (+X); mirrored for the left
    "clavicle": (0.03, 0.0, 1.43),
    "shoulder": (0.18, -0.01, 1.43),
    "elbow": (0.36, -0.02, 1.215),
    "wrist": (0.52, 0.0, 1.02),
    "hand_end": (0.58, 0.01, 0.94),
    "hip": (0.095, 0.0, 0.93),
    "knee": (0.105, 0.025, 0.5),
    "ankle": (0.11, -0.01, 0.085),
    "ball": (0.11, 0.12, 0.025),
    "toe": (0.11, 0.19, 0.02),
}
BONES = [  # name, head joint, tail joint, parent
    ("hips", "hips", "spine", None),
    ("spine", "spine", "chest", "hips"),
    ("chest", "chest", "neck", "spine"),
    ("neck", "neck", "head", "chest"),
    ("head", "head", "head_top", "neck"),
]
for s, tag in ((1, "R"), (-1, "L")):
    BONES += [
        (f"clavicle.{tag}", f"clavicle.{tag}", f"shoulder.{tag}", "chest"),
        (f"upper_arm.{tag}", f"shoulder.{tag}", f"elbow.{tag}", f"clavicle.{tag}"),
        (f"forearm.{tag}", f"elbow.{tag}", f"wrist.{tag}", f"upper_arm.{tag}"),
        (f"hand.{tag}", f"wrist.{tag}", f"hand_end.{tag}", f"forearm.{tag}"),
        (f"thigh.{tag}", f"hip.{tag}", f"knee.{tag}", "hips"),
        (f"shin.{tag}", f"knee.{tag}", f"ankle.{tag}", f"thigh.{tag}"),
        (f"foot.{tag}", f"ankle.{tag}", f"ball.{tag}", f"shin.{tag}"),
        (f"toe.{tag}", f"ball.{tag}", f"toe.{tag}", f"foot.{tag}"),
    ]


def joint(name):
    if name in JOINTS:
        return Vector(JOINTS[name])
    base, tag = name.split(".")
    return _side(LIMB[base], 1 if tag == "R" else -1)


# ---------------------------------------------------------------------------
# Body: anatomical volumes (lofted tubes, ellipsoids) merged by voxel remesh
# ---------------------------------------------------------------------------

def loft(name, points, side=(1, 0, 0), ring=20, caps=True):
    """Tube through `points` [(position, (rx, ry))]: elliptical rings
    perpendicular to the path, rx along `side` (projected), ry across."""
    bm = bmesh.new()
    rings = []
    pts = [(Vector(p), r) for p, r in points]
    sv = Vector(side)
    for i, (c, (rx, ry)) in enumerate(pts):
        a = pts[max(i - 1, 0)][0]
        b = pts[min(i + 1, len(pts) - 1)][0]
        t = (b - a).normalized()
        u = (sv - t * sv.dot(t))
        u = u.normalized() if u.length > 1e-6 else t.orthogonal().normalized()
        v = t.cross(u).normalized()
        rings.append([bm.verts.new(c + u * rx * math.cos(k * 2 * math.pi / ring) + v * ry * math.sin(k * 2 * math.pi / ring))
                      for k in range(ring)])
    for r0, r1 in zip(rings, rings[1:]):
        for k in range(ring):
            bm.faces.new((r0[k], r0[(k + 1) % ring], r1[(k + 1) % ring], r1[k]))
    if caps:
        bm.faces.new(rings[0][::-1])
        bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return o


def blob(name, radii, centre, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=16, radius=1.0, location=centre,
                                         rotation=[math.radians(a) for a in rot])
    o = bpy.context.active_object
    o.name = name
    o.scale = radii
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return o


def _body_volumes():
    vols = [
        blob("pelvis", (0.158, 0.112, 0.1), (0, -0.005, 0.965)),
        blob("glute.L", (0.065, 0.055, 0.075), (0.068, -0.05, 0.91)),
        blob("glute.R", (0.065, 0.055, 0.075), (-0.068, -0.05, 0.91)),
        loft("torso", [
            ((0, 0.0, 0.93), (0.158, 0.112)),
            ((0, 0.0, 1.06), (0.148, 0.104)),
            ((0, 0.005, 1.18), (0.158, 0.11)),
            ((0, 0.005, 1.3), (0.176, 0.118)),
            ((0, -0.005, 1.38), (0.178, 0.112)),
            ((0, -0.01, 1.43), (0.15, 0.09)),
            ((0, -0.01, 1.455), (0.09, 0.065)),
        ]),
        blob("traps", (0.15, 0.075, 0.05), (0, -0.015, 1.43)),
        blob("pecs", (0.15, 0.06, 0.07), (0, 0.06, 1.33)),
        loft("neck", [((0, -0.005, 1.42), (0.06, 0.058)), ((0, 0.005, 1.5), (0.055, 0.055)),
                      ((0, 0.012, 1.57), (0.054, 0.056))]),
        blob("cranium", (0.079, 0.098, 0.105), (0, 0.008, 1.675)),
        blob("face", (0.066, 0.06, 0.08), (0, 0.05, 1.63)),
        blob("jaw", (0.056, 0.06, 0.045), (0, 0.045, 1.585)),
    ]
    for s, tag in ((1, "R"), (-1, "L")):
        x = lambda p: _side(p, s)  # noqa: E731
        vols += [
            blob(f"deltoid.{tag}", (0.056, 0.06, 0.062), x((0.18, -0.01, 1.395))),
            loft(f"arm.{tag}", [
                (x((0.17, -0.01, 1.42)), (0.062, 0.064)),
                (x((0.26, -0.015, 1.32)), (0.056, 0.058)),
                (x((0.35, -0.02, 1.225)), (0.047, 0.048)),
                (x((0.41, -0.016, 1.16)), (0.05, 0.046)),
                (x((0.47, -0.008, 1.085)), (0.038, 0.032)),
                (x((0.515, 0.0, 1.025)), (0.03, 0.024)),
            ], side=(0, 1, 0)),
            loft(f"hand.{tag}", [
                (x((0.51, 0.0, 1.035)), (0.031, 0.022)),
                (x((0.545, 0.006, 0.99)), (0.046, 0.022)),
                (x((0.585, 0.01, 0.935)), (0.044, 0.019)),
                (x((0.61, 0.012, 0.9)), (0.034, 0.016)),
                (x((0.62, 0.012, 0.885)), (0.02, 0.012)),
            ], side=(0, 1, 0)),
            loft(f"thumb.{tag}", [
                (x((0.53, 0.03, 1.0)), (0.014, 0.014)),
                (x((0.555, 0.055, 0.965)), (0.012, 0.012)),
                (x((0.565, 0.062, 0.945)), (0.009, 0.009)),
            ]),
            # Trousers: loose cargo cut over the thigh, gathered into the boot.
            loft(f"leg.{tag}", [
                (x((0.095, 0.0, 0.96)), (0.1, 0.105)),
                (x((0.104, 0.008, 0.8)), (0.09, 0.094)),
                (x((0.106, 0.018, 0.6)), (0.07, 0.074)),
                (x((0.106, 0.026, 0.5)), (0.064, 0.068)),
                (x((0.108, 0.01, 0.37)), (0.064, 0.07)),
                (x((0.11, -0.005, 0.24)), (0.054, 0.058)),
                (x((0.11, -0.008, 0.16)), (0.05, 0.054)),
            ]),
            # Boot: shaft and foot.
            loft(f"boot_shaft.{tag}", [
                (x((0.11, -0.01, 0.22)), (0.05, 0.054)),
                (x((0.11, -0.012, 0.1)), (0.047, 0.055)),
                (x((0.11, 0.0, 0.03)), (0.05, 0.065)),
            ]),
            loft(f"boot_foot.{tag}", [
                (x((0.11, -0.07, 0.05)), (0.042, 0.045)),
                (x((0.11, -0.03, 0.055)), (0.048, 0.055)),
                (x((0.11, 0.06, 0.05)), (0.052, 0.048)),
                (x((0.11, 0.15, 0.04)), (0.048, 0.036)),
                (x((0.11, 0.2, 0.034)), (0.034, 0.026)),
            ], side=(1, 0, 0)),
        ]
    return vols


def make_body(voxel=0.006):
    vols = _body_volumes()
    body = tc.join(vols, "body")
    body.data.transform(body.matrix_world)
    body.matrix_world = Matrix.Identity(4)
    m = body.modifiers.new("Remesh", 'REMESH')
    m.mode = 'VOXEL'
    m.voxel_size = voxel
    m.adaptivity = 0.0
    tc.apply_modifiers(body)
    sm = body.modifiers.new("Smooth", 'CORRECTIVE_SMOOTH')
    sm.factor = 0.5
    sm.iterations = 8
    sm.use_only_smooth = True
    sm2 = body.modifiers.new("Smooth2", 'SMOOTH')
    sm2.factor = 0.6
    sm2.iterations = 6
    tc.apply_modifiers(body)
    # Flat soles.
    for v in body.data.vertices:
        if v.co.z < 0.012:
            v.co.z = 0.0
    return body



# ---------------------------------------------------------------------------
# Regions, gear, rig
# ---------------------------------------------------------------------------

ARM_START = Vector((0.17, 1.42))   # (|x|, z) of the shoulder in the A-pose
ARM_WRIST = Vector((0.515, 1.025))


def arm_t(co):
    """Position along the arm (0 shoulder, 1 wrist) in the A-pose plane."""
    p = Vector((abs(co.x), co.z))
    d = ARM_WRIST - ARM_START
    return (p - ARM_START).dot(d) / d.length_squared


def region(co, t):
    ax = abs(co.x)
    if co.z < 0.014:
        return RUBBER
    if co.z < 0.215 and ax > 0.04:
        return BOOT
    if co.z > 1.47 or (co.z > 1.44 and ax < 0.07):
        eyes = co.y > 0.03 and 1.648 < co.z < 1.69 and ax < 0.062
        return SKIN if eyes else BALACLAVA
    if ax > 0.16 and co.z > 0.86 and arm_t(co) > -0.05:
        return GLOVE if arm_t(co) > 1.0 else t["shirt"]
    if co.z < 0.975:
        return t["pants"]
    return t["shirt"]


def paint_by(o, fn):
    """Per-vertex paint (tfps_col, tfps_mr) from fn(position) -> finish."""
    me = o.data
    col = me.color_attributes.get("tfps_col") or me.color_attributes.new("tfps_col", 'FLOAT_COLOR', 'CORNER')
    mr = me.color_attributes.get("tfps_mr") or me.color_attributes.new("tfps_mr", 'FLOAT_COLOR', 'CORNER')
    per_vertex = [fn(v.co) for v in me.vertices]
    for li, loop in enumerate(me.loops):
        rgb, metallic, rough = per_vertex[loop.vertex_index]
        col.data[li].color = (rgb[0], rgb[1], rgb[2], 1.0)
        mr.data[li].color = (metallic, rough, 0.0, 1.0)


def shell_from(body, keep, thickness, name, finish, z_cuts=(), x_cut=None):
    """A garment shell: the body faces where keep(position) holds, thickened
    outward. Open edges near `z_cuts` / `x_cut` (|x|) are snapped onto them,
    so hems are straight."""
    tc.select_only([body])
    bpy.ops.object.duplicate()
    o = bpy.context.active_object
    o.name = name
    bm = bmesh.new()
    bm.from_mesh(o.data)
    drop = [f for f in bm.faces if not all(keep(v.co) for v in f.verts)]
    bmesh.ops.delete(bm, geom=drop, context='FACES')
    edge = {v for e in bm.edges if e.is_boundary for v in e.verts}
    for v in edge:
        for zc in z_cuts:
            if abs(v.co.z - zc) < 0.025:
                v.co.z = zc
        if x_cut is not None and abs(abs(v.co.x) - x_cut) < 0.025 and all(abs(v.co.z - zc) > 0.004 for zc in z_cuts):
            v.co.x = math.copysign(x_cut, v.co.x)
    bm.to_mesh(o.data)
    bm.free()
    m = o.modifiers.new("Solidify", 'SOLIDIFY')
    m.thickness = thickness
    m.offset = 1.0
    m.use_rim = True
    sm = o.modifiers.new("Smooth", 'SMOOTH')
    sm.factor = 0.5
    sm.iterations = 4
    tc.apply_modifiers(o)
    return part(o, finish)


def rigid(o, bone):
    g = o.vertex_groups.new(name=bone)
    g.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
    return o


def rbox(name, size, centre, finish, round_=0.3, rot=(0, 0, 0)):
    o = tc.box(name, size, centre, min(size) * round_, 3)
    if any(rot):
        o.rotation_euler = [math.radians(a) for a in rot]
        tc.select_only([o])
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    return part(o, finish)


def gear(body, t):
    g, w, a = t["gear"], t["webbing"], t["accent"]
    out = []

    def add(o, bone):
        out.append(rigid(o, bone))

    def vest(co):
        return 1.05 < co.z < 1.43 and abs(co.x) < 0.168
    add(shell_from(body, vest, 0.03, "carrier", g, (1.05, 1.43), 0.168), "chest")
    add(shell_from(body, lambda co: 0.935 < co.z < 0.985 and abs(co.x) < 0.2, 0.018, "belt", w, (0.935, 0.985)), "hips")
    add(part(loft("collar", [((0, -0.01, 1.435), (0.082, 0.076)), ((0, -0.006, 1.468), (0.07, 0.066))], caps=False), t["shirt"]), "chest")
    for s, tag in ((1, "R"), (-1, "L")):
        x = lambda p: _side(p, s)  # noqa: E731
        strap = loft(f"strap.{tag}", [
            (x((0.1, 0.12, 1.39)), (0.032, 0.006)), (x((0.1, 0.07, 1.455)), (0.032, 0.006)),
            (x((0.1, -0.03, 1.47)), (0.032, 0.006)), (x((0.1, -0.11, 1.41)), (0.032, 0.006))])
        add(part(strap, w), "chest")
        add(rbox(f"cummerbund.{tag}", (0.02, 0.17, 0.14), x((0.175, 0.0, 1.17)), w, 0.25), "chest")
        add(rbox(f"hip_pouch.{tag}", (0.05, 0.09, 0.1), x((0.175, -0.04, 0.93)), g, 0.25), "hips")
        add(part(blob(f"knee_pad.{tag}", (0.058, 0.032, 0.068), x((0.106, 0.085, 0.5))), g), f"shin.{tag}")
        knee_strap = loft(f"knee_strap.{tag}", [(x((0.106, 0.02, 0.46)), (0.07, 0.074)), (x((0.106, 0.02, 0.445)), (0.07, 0.074))], caps=False)
        add(part(knee_strap, RUBBER), f"shin.{tag}")
        add(rbox(f"patch.{tag}", (0.006, 0.055, 0.045), x((0.235, -0.01, 1.33)), a, 0.1, (0, 40 * s, 0)), f"upper_arm.{tag}")
        add(part(blob(f"knuckles.{tag}", (0.014, 0.04, 0.012), x((0.578, 0.01, 0.95))), RUBBER), f"hand.{tag}")
        gaiter = loft(f"gaiter.{tag}", [(x((0.11, -0.008, 0.195)), (0.058, 0.062)), (x((0.11, -0.008, 0.235)), (0.06, 0.064))], caps=False)
        add(part(gaiter, BOOT), f"shin.{tag}")
        cuff = loft(f"glove_cuff.{tag}", [(x((0.497, -0.003, 1.047)), (0.035, 0.03)), (x((0.53, 0.002, 1.008)), (0.034, 0.028))], side=(0, 1, 0), caps=False)
        add(part(cuff, GLOVE), f"hand.{tag}")
        add(part(tc.cylinder(f"ear.{tag}", 0.043, 0.045, x((0.09, 0.0, 1.652)), 'X', 24, 0.008), g), "head")
        add(rbox(f"rail.{tag}", (0.012, 0.14, 0.024), x((0.097, -0.005, 1.71)), METAL, 0.15), "head")
    # Front: three magazine pouches with flaps, admin pouch; back: panel, radio.
    for i, px in enumerate((-0.085, 0.0, 0.085)):
        add(rbox(f"mag{i}", (0.074, 0.05, 0.125), (px, 0.162, 1.17), g, 0.22), "chest")
        add(rbox(f"flap{i}", (0.077, 0.055, 0.03), (px, 0.164, 1.235), w, 0.25), "chest")
    add(rbox("admin", (0.17, 0.035, 0.09), (0, 0.168, 1.33), g, 0.25), "chest")
    add(rbox("admin_patch", (0.07, 0.006, 0.04), (0.04, 0.188, 1.34), a, 0.1), "chest")
    add(rbox("back_panel", (0.21, 0.05, 0.25), (0, -0.168, 1.24), g, 0.22), "chest")
    add(rbox("radio", (0.065, 0.05, 0.14), (-0.085, -0.2, 1.3), g, 0.2), "chest")
    add(part(tc.cylinder("antenna", 0.005, 0.24, (-0.095, -0.2, 1.48), 'Z', 8), RUBBER), "chest")
    add(rbox("dump", (0.15, 0.05, 0.09), (0, -0.14, 0.94), g, 0.3), "hips")
    add(rbox("holster", (0.045, 0.1, 0.16), (0.205, 0.01, 0.8), g, 0.2), "thigh.R")
    add(rbox("leg_strap", (0.02, 0.11, 0.02), (0.2, 0.0, 0.74), w, 0.2), "thigh.R")
    # Helmet: cut shell, NVG shroud, counterweight; goggles.
    add(helmet(t), "head")
    add(rbox("shroud", (0.05, 0.03, 0.045), (0, 0.118, 1.735), METAL, 0.2, (-25, 0, 0)), "head")
    add(rbox("counterweight", (0.09, 0.035, 0.06), (0, -0.112, 1.705), g, 0.25, (15, 0, 0)), "head")
    add(rbox("velcro", (0.06, 0.04, 0.008), (0, -0.03, 1.792), a, 0.1, (-12, 0, 0)), "head")
    add(part(blob("goggle_frame", (0.082, 0.032, 0.03), (0, 0.088, 1.668)), RUBBER), "head")
    add(part(blob("goggle_lens", (0.074, 0.026, 0.024), (0, 0.1, 1.668)), LENS), "head")
    add(part(loft("goggle_strap", [((0, 0.0, 1.672), (0.088, 0.104)), ((0, 0.0, 1.66), (0.088, 0.104))], caps=False), RUBBER), "head")
    return out


def helmet(t):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=40, ring_count=24, radius=1.0, location=(0, 0, 0))
    o = bpy.context.active_object
    o.name = "helmet"
    o.scale = (0.103, 0.118, 0.1)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    o.location = (0, 0.0, 1.69)
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    n = Vector((0, -0.3, 1)).normalized()
    bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0, 0, 1.655),
                           plane_no=n, clear_inner=True)
    # Cut-away over the ears (high-cut helmet).
    for v in bm.verts:
        if abs(v.co.x) > 0.07 and v.co.z < 1.69 and abs(v.co.y) < 0.06:
            v.co.z = 1.69 + (v.co.z - 1.69) * 0.3
    bm.to_mesh(o.data)
    bm.free()
    m = o.modifiers.new("Solidify", 'SOLIDIFY')
    m.thickness = 0.012
    m.offset = 1.0
    tc.apply_modifiers(o)
    return part(o, t["helmet"])


def part(o, finish):
    rgb, metallic, rough = finish
    return tc.paint(o, rgb, metallic, rough)


def make_armature():
    arm_data = bpy.data.armatures.new("OperatorRig")
    arm = bpy.data.objects.new("OperatorRig", arm_data)
    bpy.context.collection.objects.link(arm)
    tc.select_only([arm])
    bpy.ops.object.mode_set(mode='EDIT')
    eb = arm_data.edit_bones
    for name, h, tl, parent in BONES:
        b = eb.new(name)
        b.head = joint(h)
        b.tail = joint(tl)
        b.roll = 0.0
        if parent:
            b.parent = eb[parent]
            b.use_connect = False
    bpy.ops.object.mode_set(mode='OBJECT')
    return arm


def _seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))
    return (p - (a + ab * t)).length


def weight_body(body):
    """Skin weights from distances to the bone segments (3 nearest, smooth falloff)."""
    segs = [(name, joint(h), joint(tl)) for name, h, tl, _ in BONES]
    groups = {name: body.vertex_groups.new(name=name) for name, _, _, _ in BONES}
    for v in body.data.vertices:
        p = v.co
        ds = sorted(((_seg_dist(p, a, b), name) for name, a, b in segs))[:3]
        # Arms never pull the torso (and the torso never the arm) across the armpit.
        ws = [(1.0 / (d * d + 2e-4) ** 2, n) for d, n in ds]
        total = sum(w for w, _ in ws)
        for w, n in ws:
            if w / total > 0.02:
                groups[n].add([v.index], w / total, 'REPLACE')


def material(asset):
    """Painted material plus cloth detail on fabric (roughness above 0.84):
    creases from stretched noise and a fine weave, both in the normal bake,
    and a slightly darker, uneven tone in the folds."""
    m = tc.painted_material(f"M_{asset}_Proc", grain=0.07, bump_strength=0.25)
    nt = m.node_tree
    bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    mr = next(n for n in nt.nodes if n.type == 'ATTRIBUTE' and n.attribute_name == "tfps_mr")
    coord = next(n for n in nt.nodes if n.type == 'TEX_COORD')
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(mr.outputs["Color"], sep.inputs["Color"])
    cloth = tc.node(nt, "ShaderNodeMapRange")
    cloth.inputs["From Min"].default_value = 0.8
    cloth.inputs["From Max"].default_value = 0.86
    nt.links.new(sep.outputs["Green"], cloth.inputs["Value"])
    # Creases: noise stretched along the vertical (limbs and torso hang
    # down), sharpened into folds a few millimetres deep.
    mapping = nt.nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (1.0, 1.0, 0.3)
    nt.links.new(coord.outputs["Object"], mapping.inputs["Vector"])
    folds = tc.node(nt, "ShaderNodeTexNoise")
    folds.inputs["Scale"].default_value = 16.0
    folds.inputs["Detail"].default_value = 4.0
    folds.inputs["Distortion"].default_value = 2.2
    nt.links.new(mapping.outputs["Vector"], folds.inputs["Vector"])
    # Ripstop: a 5 mm grid of slightly raised threads.
    rip = None
    for axis in ('X', 'Z'):
        wave = tc.node(nt, "ShaderNodeTexWave")
        wave.bands_direction = axis
        wave.inputs["Scale"].default_value = 63.0   # period 0.314 / scale m: about 5 mm
        wave.inputs["Distortion"].default_value = 0.0
        nt.links.new(coord.outputs["Object"], wave.inputs["Vector"])
        sharp = tc.node(nt, "ShaderNodeMapRange")
        sharp.inputs["From Min"].default_value = 0.85
        sharp.inputs["From Max"].default_value = 1.0
        nt.links.new(wave.outputs["Fac"], sharp.inputs["Value"])
        if rip is None:
            rip = sharp
        else:
            mx = tc.node(nt, "ShaderNodeMath", operation='MAXIMUM')
            nt.links.new(rip.outputs[0], mx.inputs[0])
            nt.links.new(sharp.outputs["Result"], mx.inputs[1])
            rip = mx
    # MOLLE on the gear (roughness 0.86: vest, pouches, belt): 25 mm webbing
    # rows slit every 38 mm; the gaps are recessed and darker.
    gear_lo = tc.node(nt, "ShaderNodeMapRange")
    gear_lo.inputs["From Min"].default_value = 0.85
    gear_lo.inputs["From Max"].default_value = 0.855
    nt.links.new(sep.outputs["Green"], gear_lo.inputs["Value"])
    gear_hi = tc.node(nt, "ShaderNodeMapRange")
    gear_hi.inputs["From Min"].default_value = 0.875
    gear_hi.inputs["From Max"].default_value = 0.87
    nt.links.new(sep.outputs["Green"], gear_hi.inputs["Value"])
    gear = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY')
    nt.links.new(gear_lo.outputs["Result"], gear.inputs[0])
    nt.links.new(gear_hi.outputs["Result"], gear.inputs[1])
    rows = tc.node(nt, "ShaderNodeTexWave")
    rows.bands_direction = 'Z'
    rows.inputs["Scale"].default_value = 6.3   # 50 mm period: 25 mm webbing, 25 mm gap
    rows.inputs["Distortion"].default_value = 0.0
    nt.links.new(coord.outputs["Object"], rows.inputs["Vector"])
    rows_sq = tc.node(nt, "ShaderNodeMapRange")
    rows_sq.inputs["From Min"].default_value = 0.45
    rows_sq.inputs["From Max"].default_value = 0.55
    nt.links.new(rows.outputs["Fac"], rows_sq.inputs["Value"])
    slits = tc.node(nt, "ShaderNodeTexWave")
    slits.bands_direction = 'X'
    slits.inputs["Scale"].default_value = 8.3   # slits every 38 mm
    slits.inputs["Distortion"].default_value = 0.0
    nt.links.new(coord.outputs["Object"], slits.inputs["Vector"])
    slits_sq = tc.node(nt, "ShaderNodeMapRange")
    slits_sq.inputs["From Min"].default_value = 0.04
    slits_sq.inputs["From Max"].default_value = 0.1
    nt.links.new(slits.outputs["Fac"], slits_sq.inputs["Value"])
    molle = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY')
    nt.links.new(rows_sq.outputs["Result"], molle.inputs[0])
    nt.links.new(slits_sq.outputs["Result"], molle.inputs[1])
    molle_g = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY')
    nt.links.new(molle.outputs[0], molle_g.inputs[0])
    nt.links.new(gear.outputs[0], molle_g.inputs[1])
    # Height: folds + weave on all fabric, webbing on the gear.
    h1 = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY_ADD')
    nt.links.new(rip.outputs[0], h1.inputs[0])
    h1.inputs[1].default_value = 0.006
    nt.links.new(folds.outputs["Fac"], h1.inputs[2])
    masked = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY')
    nt.links.new(h1.outputs["Value"], masked.inputs[0])
    nt.links.new(cloth.outputs["Result"], masked.inputs[1])
    height = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY_ADD')
    nt.links.new(molle_g.outputs[0], height.inputs[0])
    height.inputs[1].default_value = 0.35
    nt.links.new(masked.outputs["Value"], height.inputs[2])
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 1.0
    bump.inputs["Distance"].default_value = 0.012
    nt.links.new(height.outputs["Value"], bump.inputs["Height"])
    old = bsdf.inputs["Normal"].links[0].from_node
    nt.links.new(bump.outputs["Normal"], old.inputs["Normal"])
    # Darker in the folds.
    shade = tc.node(nt, "ShaderNodeMapRange")
    shade.inputs["To Min"].default_value = 0.8
    shade.inputs["To Max"].default_value = 1.08
    nt.links.new(folds.outputs["Fac"], shade.inputs["Value"])
    shade_mix = tc.node(nt, "ShaderNodeMix", data_type='FLOAT')
    nt.links.new(cloth.outputs["Result"], shade_mix.inputs["Factor"])
    shade_mix.inputs[2].default_value = 1.0
    nt.links.new(shade.outputs["Result"], shade_mix.inputs[3])
    gap = tc.node(nt, "ShaderNodeMath", operation='SUBTRACT')
    nt.links.new(gear.outputs[0], gap.inputs[0])
    nt.links.new(molle_g.outputs[0], gap.inputs[1])
    gap_dark = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY_ADD')
    nt.links.new(gap.outputs[0], gap_dark.inputs[0])
    gap_dark.inputs[1].default_value = -0.3
    gap_dark.inputs[2].default_value = 1.0
    shade_total = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY')
    nt.links.new(shade_mix.outputs[0], shade_total.inputs[0])
    nt.links.new(gap_dark.outputs[0], shade_total.inputs[1])
    base_src = bsdf.inputs["Base Color"].links[0].from_socket
    tint = tc.node(nt, "ShaderNodeVectorMath", operation='SCALE')
    nt.links.new(base_src, tint.inputs[0])
    nt.links.new(shade_total.outputs[0], tint.inputs["Scale"])
    nt.links.new(tint.outputs["Vector"], bsdf.inputs["Base Color"])
    return m


def build_team(team, colours):
    asset = f"Operator{team}"
    tc.reset_scene()
    body = make_body()
    body = decimate(body, 26000)
    paint_by(body, lambda co: region(co, colours))
    parts = gear(body, colours)
    weight_body(body)
    arms = first_person_arms(body, parts, f"SM_{asset}Arms", GLOVES[team])
    arms.hide_render = True   # not in the operator preview
    # Third person: the same modelled gloves in place of the body's hands.
    cut_hands(body)
    parts = [o for o in parts if not any(g.name.startswith("hand.") for g in o.vertex_groups)]
    parts += [gloved_hand(tag, GLOVES[team]) for tag in ("R", "L")]
    mesh = tc.join([body] + parts, f"SM_{asset}")
    mesh.data.transform(mesh.matrix_world)
    mesh.matrix_world = Matrix.Identity(4)
    tc.shade_auto_smooth(mesh, 60.0)
    tc.uv_unwrap(mesh, 0.002)
    mat = material(asset)
    mesh.data.materials.append(mat)
    arm = make_armature()
    mod = mesh.modifiers.new("Armature", 'ARMATURE')
    mod.object = arm
    mesh.parent = arm
    out_dir = os.path.join(tc.REPO, "Assets", "Exports", asset)
    files, bc, nrm, orm = tc.bake_textures(mesh, mat, out_dir, asset)
    baked = tc.make_baked_material(asset, bc, nrm, orm)
    mesh.data.materials.clear()
    mesh.data.materials.append(baked)
    files["fbx_lod0"] = tc.export_fbx([arm, mesh], os.path.join(out_dir, f"SM_{asset}.fbx"))
    files["preview"] = preview(mesh, os.path.join(out_dir, "preview.png"))
    export_arms(arms, arm, mat, f"{asset}Arms")
    blend_path = os.path.join(tc.REPO, "Assets", "Blender", f"{asset}.blend")
    bpy.ops.wm.save_as_mainfile(filepath=blend_path, compress=True)
    bpy.ops.file.make_paths_relative()
    bpy.ops.wm.save_as_mainfile(filepath=blend_path, compress=True)
    files["blend"] = os.path.relpath(blend_path, tc.REPO)
    manifest = {
        "asset": asset,
        "mesh": f"SM_{asset}",
        "dimensions_m": tc.dimensions(mesh),
        "triangles_lod0": tc.triangle_count(mesh),
        "texture_size": tc.TEX_SIZE,
        "files": files,
        "notes": "Skinned operator; faces +Y (Godot -Z), A-pose. Bones: see build_character.py.",
        "bones": [b[0] for b in BONES],
        "hand_frames": {tag: [round(x, 6) for row in hold_frame(tag) for x in row] for tag in ("R", "L")},
    }
    import json
    with open(os.path.join(out_dir, "manifest.json"), "w", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print("TFPS_ASSET_DONE", json.dumps({"asset": asset, "triangles": manifest["triangles_lod0"]}))


# ---------------------------------------------------------------------------
# First-person gloved hands, modelled in their holding poses
# ---------------------------------------------------------------------------
# Each hand is modelled in a "hold frame" with Godot's axes (x right, y up,
# z back), in metres. Right hand: the frame of a weapon from build_weapons.py
# (origin at the top of the pistol grip, barrel along -z); the hand wraps the
# grip, index finger on the trigger, thumb along the left of the receiver.
# Left hand: a handguard of radius 2.2 cm along -z through the origin; the
# palm under it, fingers wrapping up the right side, thumb along the left.
# The game places the hand bone so the hold frame lands on the weapon
# (hand_frames in the manifest: hold frame -> skeleton space at rest).

def _fingers_left():
    rho = 0.0315
    out = []
    #           MCP (x, y, z)            PIP, DIP, tip angles (deg)    radii
    for mcp, angles, r0 in (((0.014, -0.031, -0.026), (88, 128, 155), 0.0092),
                            ((0.016, -0.033, -0.005), (95, 138, 168), 0.0095),
                            ((0.014, -0.035, 0.015), (97, 140, 168), 0.009),
                            ((0.01, -0.037, 0.033), (100, 140, 164), 0.0082)):
        pts = [mcp] + [(rho * math.sin(math.radians(a)), -rho * math.cos(math.radians(a)), mcp[2]) for a in angles]
        out.append((pts, r0))
    return out


HANDS = {
    "R": {
        "wrist": (0.04, -0.05, 0.112),
        "palm": [((0.042, -0.05, 0.118), (0.031, 0.024)), ((0.036, -0.047, 0.085), (0.035, 0.019)),
                 ((0.032, -0.045, 0.045), (0.044, 0.017)), ((0.029, -0.046, 0.006), (0.043, 0.014))],
        "palm_side": (0, 1, 0),
        "fingers": [
            ([(0.027, -0.014, 0.008), (0.025, -0.013, -0.025), (0.014, -0.015, -0.036), (0.005, -0.017, -0.027)], 0.0092),
            ([(0.026, -0.035, -0.006), (-0.012, -0.037, -0.017), (-0.025, -0.039, 0.01), (-0.022, -0.04, 0.03)], 0.0095),
            ([(0.025, -0.057, 0.0), (-0.012, -0.058, -0.008), (-0.025, -0.059, 0.016), (-0.022, -0.06, 0.033)], 0.009),
            ([(0.023, -0.077, 0.003), (-0.008, -0.079, -0.008), (-0.023, -0.081, 0.01), (-0.021, -0.082, 0.024)], 0.0082),
        ],
        "thumb": ([(0.022, -0.016, 0.075), (-0.012, 0.0, 0.047), (-0.024, 0.004, 0.014), (-0.024, 0.004, -0.01)], 0.0115),
        "pads": [((0.014, -0.018, 0.062), (0.016, 0.018, 0.024)), ((0.02, -0.072, 0.07), (0.013, 0.022, 0.02))],
        "knuckles": ((0.047, -0.045, 0.012), (0.008, 0.04, 0.016)),
        "forearm": (0.17, -0.5, 0.85),   # toward the elbow in the usual poses (measured in game)
    },
    "L": {
        "wrist": (-0.052, -0.06, 0.06),
        "palm": [((-0.056, -0.063, 0.066), (0.03, 0.024)), ((-0.032, -0.05, 0.034), (0.035, 0.019)),
                 ((-0.008, -0.041, 0.006), (0.044, 0.017)), ((0.013, -0.035, 0.0), (0.043, 0.014))],
        "palm_side": (0, 0, 1),
        "fingers": _fingers_left(),
        "thumb": ([(-0.036, -0.047, 0.03), (-0.031, -0.03, 0.0), (-0.033, -0.013, -0.026), (-0.031, -0.006, -0.046)], 0.0115),
        "pads": [((-0.03, -0.044, 0.012), (0.016, 0.02, 0.022))],
        "knuckles": ((0.022, -0.048, 0.004), (0.009, 0.012, 0.042)),
        "forearm": (-0.68, -0.18, 0.71),
    },
}
# Hold frame (Godot axes) -> Blender axes.
_G2B = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))


def hold_frame(tag):
    """Hold frame -> skeleton space at rest: the modelled wrist on the rest wrist joint."""
    return Matrix.Translation(joint(f"wrist.{tag}")) @ _G2B @ Matrix.Translation(-Vector(HANDS[tag]["wrist"]))


def _finger(name, pts, r0):
    taper = [r0, r0 * 0.93, r0 * 0.86, r0 * 0.8]
    o = loft(name, [(p, (r, r)) for p, r in zip(pts, taper)], ring=14)
    tip = blob(name + "_tip", (taper[-1],) * 3, pts[-1])
    return [o, tip]


def gloved_hand(tag, glove):
    h = HANDS[tag]
    vols = [loft(f"palm.{tag}", h["palm"], side=h["palm_side"])]
    for i, (pts, r0) in enumerate(h["fingers"]):
        vols += _finger(f"finger{i}.{tag}", pts, r0)
        vols.append(blob(f"knuckle{i}.{tag}", (r0 * 1.1,) * 3, pts[0]))
    vols += _finger(f"thumb.{tag}", *h["thumb"])
    for i, (c, r) in enumerate(h["pads"]):
        vols.append(blob(f"pad{i}.{tag}", r, c))
    # Gauntlet over the sleeve's end, along the forearm.
    w, d = Vector(h["wrist"]), Vector(h["forearm"]).normalized()
    vols.append(loft(f"cuff.{tag}", [(w - d * 0.01, (0.031, 0.027)), (w + d * 0.032, (0.034, 0.03))], ring=20))
    hand = tc.join(vols, f"glove.{tag}")
    hand.data.transform(hand.matrix_world)
    hand.matrix_world = Matrix.Identity(4)
    m = hand.modifiers.new("Remesh", 'REMESH')
    m.mode = 'VOXEL'
    m.voxel_size = 0.0016
    m.adaptivity = 0.0
    tc.apply_modifiers(hand)
    sm = hand.modifiers.new("Smooth", 'CORRECTIVE_SMOOTH')
    sm.factor = 0.5
    sm.iterations = 6
    sm.use_only_smooth = True
    tc.apply_modifiers(hand)
    hand = decimate(hand, 5000)
    part(hand, glove)
    c, r = h["knuckles"]
    guard = part(blob(f"knuckle_guard.{tag}", r, c), RUBBER)
    hand = tc.join([hand, guard], f"hand_{tag}")
    hand.data.transform(hold_frame(tag))
    hand.matrix_world = Matrix.Identity(4)
    g = hand.vertex_groups.new(name=f"hand.{tag}")
    g.add(list(range(len(hand.data.vertices))), 1.0, 'REPLACE')
    return hand


ARM_BONES = ("upper_arm", "forearm", "hand")


def hand_weights_to_forearm(o):
    """The hand bone is placed freely on the weapon (modelled hands), so the
    sleeve around the wrist follows the forearm only."""
    vg = {g.name: g for g in o.vertex_groups}
    for v in o.data.vertices:
        for ge in list(v.groups):
            name = o.vertex_groups[ge.group].name
            if name.startswith("hand."):
                vg["forearm." + name[-1]].add([v.index], ge.weight, 'ADD')
                o.vertex_groups[ge.group].remove([v.index])


def cut_hands(body):
    """Removes the body's own (mitten) hands beyond the wrist; the modelled
    gloves replace them."""
    bm = bmesh.new()
    bm.from_mesh(body.data)
    hand = lambda co: abs(co.x) > 0.135 and co.z > 0.85 and arm_t(co) >= 0.995  # noqa: E731
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if any(hand(v.co) for v in f.verts)], context='FACES')
    bm.to_mesh(body.data)
    bm.free()
    hand_weights_to_forearm(body)


def first_person_arms(body, parts, name, glove):
    """Sleeves, gloves, cuffs and patches for the first-person view: the
    arm region of the weighted body plus copies of the gear bound to arm
    bones, as one mesh (call after weight_body)."""
    tc.select_only([body])
    bpy.ops.object.duplicate()
    arms = bpy.context.active_object
    bm = bmesh.new()
    bm.from_mesh(arms.data)
    # Sleeves only: the gloves are modelled separately (gloved_hand).
    keep = lambda co: abs(co.x) > 0.135 and co.z > 0.85 and -0.12 < arm_t(co) < 0.995  # noqa: E731
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if not all(keep(v.co) for v in f.verts)], context='FACES')
    bm.to_mesh(arms.data)
    bm.free()
    hand_weights_to_forearm(arms)
    extra = []
    for o in parts:
        names = [g.name.split(".")[0] for g in o.vertex_groups]
        if any(n in ARM_BONES for n in names) and "hand" not in names:
            tc.select_only([o])
            bpy.ops.object.duplicate()
            extra.append(bpy.context.active_object)
    extra += [gloved_hand(tag, glove) for tag in ("R", "L")]
    arms = tc.join([arms] + extra, name)
    arms.data.transform(arms.matrix_world)
    arms.matrix_world = Matrix.Identity(4)
    return arms


def export_arms(arms, rig, mat, asset):
    arms.hide_render = False
    tc.shade_auto_smooth(arms, 60.0)
    tc.uv_unwrap(arms, 0.002)
    arms.data.materials.clear()
    arms.data.materials.append(mat)
    mod = arms.modifiers.new("Armature", 'ARMATURE')
    mod.object = rig
    arms.parent = rig
    out_dir = os.path.join(tc.REPO, "Assets", "Exports", asset)
    files, bc, nrm, orm = tc.bake_textures(arms, mat, out_dir, asset)
    baked = tc.make_baked_material(asset, bc, nrm, orm)
    arms.data.materials.clear()
    arms.data.materials.append(baked)
    files["fbx_lod0"] = tc.export_fbx([rig, arms], os.path.join(out_dir, f"SM_{asset}.fbx"))
    manifest = {
        "asset": asset,
        "mesh": f"SM_{asset}",
        "triangles_lod0": tc.triangle_count(arms),
        "texture_size": tc.TEX_SIZE,
        "files": files,
        "notes": "First-person arms: the operator's sleeves and gloves on the full operator rig. hand_frames: per hand, the hold frame (Godot axes, see HANDS in build_character.py) to skeleton space at rest, row-major 4x4.",
        "hand_frames": {tag: [round(x, 6) for row in hold_frame(tag) for x in row] for tag in ("R", "L")},
    }
    import json
    with open(os.path.join(out_dir, "manifest.json"), "w", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")


def decimate(o, target_tris):
    tris = tc.triangle_count(o)
    if tris > target_tris:
        m = o.modifiers.new("Decimate", 'DECIMATE')
        m.ratio = target_tris / tris
        m.use_collapse_triangulate = False
        tc.apply_modifiers(o)
    return o


def preview(o, path):
    sc = bpy.context.scene
    bpy.ops.mesh.primitive_plane_add(size=8, location=(0, 0, 0))
    ground = bpy.context.active_object
    gm, _, gb, _ = tc.new_material("M_PreviewGround")
    gb.inputs["Base Color"].default_value = (0.2, 0.2, 0.2, 1.0)
    ground.data.materials.append(gm)
    cam_data = bpy.data.cameras.new("PreviewCam")
    cam_data.lens = 60
    cam = bpy.data.objects.new("PreviewCam", cam_data)
    sc.collection.objects.link(cam)
    target = Vector((0, 0, 0.95))
    cam.location = Vector((1.5, 3.2, 1.45))
    cam.rotation_euler = (target - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.camera = cam
    sun_data = bpy.data.lights.new("Sun", 'SUN')
    sun_data.energy = 3.0
    sun = bpy.data.objects.new("Sun", sun_data)
    sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(150))
    sc.collection.objects.link(sun)
    world = bpy.data.worlds.new("PreviewWorld")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.45, 0.55, 0.7, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.7
    sc.world = world
    sc.render.resolution_x = 720
    sc.render.resolution_y = 960
    sc.cycles.samples = 64
    sc.render.filepath = path
    sc.render.image_settings.file_format = 'PNG'
    bpy.ops.render.render(write_still=True)
    for obj in (ground, cam, sun):
        bpy.data.objects.remove(obj, do_unlink=True)
    sc.cycles.samples = tc.BAKE_SAMPLES
    return os.path.relpath(path, tc.REPO)


def build():
    only = os.environ.get("TFPS_TEAM")
    for team, colours in TEAMS.items():
        if only and only != team:
            continue
        build_team(team, colours)


if __name__ == "__main__":
    build()
