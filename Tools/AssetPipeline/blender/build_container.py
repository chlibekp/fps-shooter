"""Open-ended 20 ft style shipping container shell, 6.0 x 2.5 x 2.6 m, corrugated steel.

Matches the 'container' solids in test_arena (open end facing -X / west in the map).
blender -b --factory-startup --python Tools/AssetPipeline/blender/build_container.py
"""
import os
import sys

import bmesh

sys.path.insert(0, os.path.dirname(__file__))
import bpy  # noqa: E402
import tfps_common as tc  # noqa: E402

ASSET = "ShippingContainer"
L, W, H = 6.0, 2.5, 2.6
POST = 0.15
PITCH = 0.278
DEPTH = 0.036
SHEET = 0.004


def corrugated_sheet(name, length, height):
    """Corrugated panel in the XZ plane (x along `length`, corrugation depth along -Y).

    Built from quads only: outer zigzag, inner zigzag offset by the sheet
    thickness, and top/bottom caps.
    """
    flat = PITCH * 0.25
    slope = PITCH * 0.25
    xs, ys = [], []
    x = 0.0
    while x < length - 1e-6:
        for dx, d in ((0.0, 0.0), (flat, 0.0), (slope, DEPTH), (flat, DEPTH), (slope, 0.0)):
            x2 = min(x + dx, length)
            if not xs or abs(x2 - xs[-1]) > 1e-6 or d != ys[-1]:
                xs.append(x2)
                ys.append(d)
            x = x2
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    out_b = [bm.verts.new((xv, -yv, 0.0)) for xv, yv in zip(xs, ys)]
    out_t = [bm.verts.new((xv, -yv, height)) for xv, yv in zip(xs, ys)]
    in_b = [bm.verts.new((xv, -yv + SHEET, 0.0)) for xv, yv in zip(xs, ys)]
    in_t = [bm.verts.new((xv, -yv + SHEET, height)) for xv, yv in zip(xs, ys)]
    for i in range(len(xs) - 1):
        bm.faces.new((out_b[i], out_b[i + 1], out_t[i + 1], out_t[i]))
        bm.faces.new((in_b[i + 1], in_b[i], in_t[i], in_t[i + 1]))
        bm.faces.new((out_t[i], out_t[i + 1], in_t[i + 1], in_t[i]))
        bm.faces.new((in_b[i], in_b[i + 1], out_b[i + 1], out_b[i]))
    bm.faces.new((in_b[0], out_b[0], out_t[0], in_t[0]))
    n = len(xs) - 1
    bm.faces.new((out_b[n], in_b[n], in_t[n], out_t[n]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return o


def painted_steel_material():
    m, nt, bsdf, coord = tc.new_material("M_ContainerSteel_Proc")
    paint = (0.05, 0.12, 0.20, 1.0)
    rust_dark = (0.16, 0.07, 0.03, 1.0)
    rust_light = (0.42, 0.20, 0.08, 1.0)
    noise = tc.node(nt, "ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 2.5
    noise.inputs["Detail"].default_value = 10.0
    noise.inputs["Roughness"].default_value = 0.65
    nt.links.new(coord.outputs["Object"], noise.inputs["Vector"])
    streak_map = tc.node(nt, "ShaderNodeMapping")
    streak_map.inputs["Scale"].default_value = (8.0, 8.0, 0.6)
    nt.links.new(coord.outputs["Object"], streak_map.inputs["Vector"])
    streaks = tc.node(nt, "ShaderNodeTexNoise")
    streaks.inputs["Scale"].default_value = 3.0
    streaks.inputs["Detail"].default_value = 6.0
    nt.links.new(streak_map.outputs["Vector"], streaks.inputs["Vector"])
    sep = tc.node(nt, "ShaderNodeSeparateXYZ")
    nt.links.new(coord.outputs["Object"], sep.inputs["Vector"])
    low = tc.node(nt, "ShaderNodeMapRange")
    low.inputs["From Min"].default_value = 0.0
    low.inputs["From Max"].default_value = 1.2
    low.inputs["To Min"].default_value = 0.22
    low.inputs["To Max"].default_value = 0.0
    nt.links.new(sep.outputs["Z"], low.inputs["Value"])
    amount = tc.node(nt, "ShaderNodeMath", operation='ADD')
    nt.links.new(noise.outputs["Fac"], amount.inputs[0])
    nt.links.new(low.outputs["Result"], amount.inputs[1])
    amount2 = tc.node(nt, "ShaderNodeMath", operation='ADD')
    nt.links.new(amount.outputs["Value"], amount2.inputs[0])
    streak_scaled = tc.node(nt, "ShaderNodeMath", operation='MULTIPLY')
    streak_scaled.inputs[1].default_value = 0.25
    nt.links.new(streaks.outputs["Fac"], streak_scaled.inputs[0])
    nt.links.new(streak_scaled.outputs["Value"], amount2.inputs[1])
    rust_mask = tc.ramp(nt, [(0.0, (0, 0, 0, 1)), (0.80, (0, 0, 0, 1)), (0.86, (1, 1, 1, 1)), (1.0, (1, 1, 1, 1))])
    nt.links.new(amount2.outputs["Value"], rust_mask.inputs["Fac"])
    rust_col = tc.ramp(nt, [(0.0, rust_dark), (1.0, rust_light)])
    nt.links.new(streaks.outputs["Fac"], rust_col.inputs["Fac"])
    paint_var = tc.ramp(nt, [(0.0, (paint[0] * 0.8, paint[1] * 0.8, paint[2] * 0.8, 1)), (1.0, paint)])
    nt.links.new(noise.outputs["Fac"], paint_var.inputs["Fac"])
    mix = tc.node(nt, "ShaderNodeMix", data_type='RGBA')
    nt.links.new(rust_mask.outputs["Color"], mix.inputs["Factor"])
    nt.links.new(paint_var.outputs["Color"], mix.inputs[6])
    nt.links.new(rust_col.outputs["Color"], mix.inputs[7])
    nt.links.new(mix.outputs[2], bsdf.inputs["Base Color"])
    rough = tc.node(nt, "ShaderNodeMix", data_type='FLOAT')
    rough.inputs["A"].default_value = 0.48
    rough.inputs["B"].default_value = 0.88
    sep_mask = tc.node(nt, "ShaderNodeSeparateColor")
    nt.links.new(rust_mask.outputs["Color"], sep_mask.inputs["Color"])
    nt.links.new(sep_mask.outputs["Red"], rough.inputs["Factor"])
    nt.links.new(rough.outputs["Result"], bsdf.inputs["Roughness"])
    bump = tc.node(nt, "ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.25
    bump.inputs["Distance"].default_value = 0.002
    nt.links.new(sep_mask.outputs["Red"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    m["tfps_metallic"] = 0.0
    return m


def build():
    tc.reset_scene()
    parts = []
    span = L - 2 * POST
    # Side walls (corrugations face outward).
    for side in (-1, 1):
        s = corrugated_sheet(f"side{side}", span, H - 0.2)
        s.location = (-span / 2, side * (W / 2 - 0.02), 0.1)
        if side < 0:
            s.scale = (1, 1, 1)
        else:
            s.rotation_euler = (0, 0, 3.14159265)
            s.location = (span / 2, side * (W / 2 - 0.02), 0.1)
        tc.select_only([s])
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        parts.append(s)
    # Closed end wall (east, +X), vertical corrugations.
    e = corrugated_sheet("end", W - 2 * POST, H - 0.2)
    e.rotation_euler = (0, 0, 1.5707963)
    e.location = (L / 2 - 0.02, -(W - 2 * POST) / 2, 0.1)
    tc.select_only([e])
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    parts.append(e)
    # Roof (slightly domed panel approximated by a flat plate) and floor.
    parts.append(tc.box("roof", (L - 0.05, W - 0.05, 0.03), (0, 0, H - 0.05), bevel=0.005))
    parts.append(tc.box("floor", (L - 0.05, W - 0.05, 0.05), (0, 0, 0.075), bevel=0.005))
    # Corner posts.
    for sx in (-1, 1):
        for sy in (-1, 1):
            parts.append(tc.box("post", (POST, POST, H), (sx * (L / 2 - POST / 2), sy * (W / 2 - POST / 2), H / 2), bevel=0.01))
            # ISO corner castings.
            for z in (0.06, H - 0.06):
                parts.append(tc.box("casting", (0.178, 0.162, 0.118), (sx * (L / 2 - 0.089), sy * (W / 2 - 0.081), z), bevel=0.008))
    # Top and bottom side rails.
    for sy in (-1, 1):
        for z, h in ((0.05, 0.1), (H - 0.06, 0.12)):
            parts.append(tc.box("rail", (L - 2 * POST, 0.08, h), (0, sy * (W / 2 - 0.04), z), bevel=0.006))
    # Door end frame (west, -X): header and sill; the doors are removed (open end).
    parts.append(tc.box("header", (0.15, W - 2 * POST, 0.22), (-L / 2 + 0.075, 0, H - 0.11), bevel=0.008))
    parts.append(tc.box("sill", (0.15, W - 2 * POST, 0.12), (-L / 2 + 0.075, 0, 0.06), bevel=0.008))
    # Hinge blocks on the open end posts.
    for sy in (-1, 1):
        for z in (0.45, 1.3, 2.15):
            parts.append(tc.box("hinge", (0.05, 0.06, 0.12), (-L / 2 - 0.02, sy * (W / 2 - 0.05), z), bevel=0.006))
    shell = tc.join(parts, ASSET)
    tc.set_origin_floor_center(shell)
    tc.shade_auto_smooth(shell, 30.0)
    tc.uv_unwrap(shell, island_margin=0.002)
    shell.data.materials.append(painted_steel_material())
    lod1 = tc.make_lod(shell, 0.3, f"{ASSET}_LOD1")
    col = []
    name = f"SM_{ASSET}"
    col.append(tc.collision_box(name, 0, (L, 0.1, H), (0, -W / 2 + 0.05, H / 2)))
    col.append(tc.collision_box(name, 1, (L, 0.1, H), (0, W / 2 - 0.05, H / 2)))
    col.append(tc.collision_box(name, 2, (0.1, W - 0.2, H), (L / 2 - 0.05, 0, H / 2)))
    col.append(tc.collision_box(name, 3, (L, W, 0.1), (0, 0, H - 0.05)))
    col.append(tc.collision_box(name, 4, (L, W, 0.1), (0, 0, 0.05)))
    return tc.finish_asset(ASSET, shell, lod1, col, shell.data.materials[0],
                           notes="Open end at -X. Matches 'container' solids in test_arena. Material: metal.")


if __name__ == "__main__":
    build()
