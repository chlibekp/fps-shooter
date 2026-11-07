"""Concrete jersey barrier, 3.0 m x 0.6 m x 0.9 m, with forklift slots.

blender -b --factory-startup --python Tools/AssetPipeline/blender/build_barrier.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import bpy  # noqa: E402
import tfps_common as tc  # noqa: E402

ASSET = "JerseyBarrier"
LENGTH = 3.0
PROFILE = [(-0.30, 0.0), (0.30, 0.0), (0.30, 0.08), (0.17, 0.34), (0.08, 0.90), (-0.08, 0.90), (-0.17, 0.34), (-0.30, 0.08)]


def concrete_material():
    m, nt, bsdf, coord = tc.new_material("M_Concrete_Proc")
    noise = tc.node(nt, "ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 6.0
    noise.inputs["Detail"].default_value = 12.0
    noise.inputs["Roughness"].default_value = 0.6
    nt.links.new(coord.outputs["Object"], noise.inputs["Vector"])
    fine = tc.node(nt, "ShaderNodeTexNoise")
    fine.inputs["Scale"].default_value = 90.0
    fine.inputs["Detail"].default_value = 4.0
    nt.links.new(coord.outputs["Object"], fine.inputs["Vector"])
    pits = tc.node(nt, "ShaderNodeTexVoronoi", feature='DISTANCE_TO_EDGE')
    pits.inputs["Scale"].default_value = 55.0
    nt.links.new(coord.outputs["Object"], pits.inputs["Vector"])
    col = tc.ramp(nt, [(0.0, (0.20, 0.20, 0.19, 1)), (0.5, (0.32, 0.31, 0.30, 1)), (1.0, (0.40, 0.39, 0.37, 1))])
    nt.links.new(noise.outputs["Fac"], col.inputs["Fac"])
    # Dirt and water staining near the ground.
    sep = tc.node(nt, "ShaderNodeSeparateXYZ")
    nt.links.new(coord.outputs["Object"], sep.inputs["Vector"])
    dirt = tc.node(nt, "ShaderNodeMapRange")
    dirt.inputs["From Min"].default_value = 0.0
    dirt.inputs["From Max"].default_value = 0.3
    dirt.inputs["To Min"].default_value = 0.6
    dirt.inputs["To Max"].default_value = 1.0
    nt.links.new(sep.outputs["Z"], dirt.inputs["Value"])
    mul = tc.node(nt, "ShaderNodeMix", data_type='RGBA', blend_type='MULTIPLY')
    mul.inputs["Factor"].default_value = 1.0
    nt.links.new(col.outputs["Color"], mul.inputs[6])
    g = tc.node(nt, "ShaderNodeCombineColor")
    for ch in ("Red", "Green", "Blue"):
        nt.links.new(dirt.outputs["Result"], g.inputs[ch])
    nt.links.new(g.outputs["Color"], mul.inputs[7])
    nt.links.new(mul.outputs[2], bsdf.inputs["Base Color"])
    rough = tc.node(nt, "ShaderNodeMapRange")
    rough.inputs["To Min"].default_value = 0.78
    rough.inputs["To Max"].default_value = 0.95
    nt.links.new(fine.outputs["Fac"], rough.inputs["Value"])
    nt.links.new(rough.outputs["Result"], bsdf.inputs["Roughness"])
    pit_mask = tc.node(nt, "ShaderNodeMath", operation='LESS_THAN')
    pit_mask.inputs[1].default_value = 0.06
    nt.links.new(pits.outputs["Distance"], pit_mask.inputs[0])
    height = tc.node(nt, "ShaderNodeMath", operation='SUBTRACT')
    nt.links.new(fine.outputs["Fac"], height.inputs[0])
    nt.links.new(pit_mask.outputs["Value"], height.inputs[1])
    bump = tc.node(nt, "ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.45
    bump.inputs["Distance"].default_value = 0.003
    nt.links.new(height.outputs["Value"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    m["tfps_metallic"] = 0.0
    return m


def build():
    tc.reset_scene()
    body = tc.extrude_profile(ASSET, PROFILE, LENGTH)
    # Chamfer the long edges a little (cast concrete never has razor edges).
    bev = body.modifiers.new("Bevel", 'BEVEL')
    bev.width = 0.015
    bev.segments = 2
    bev.limit_method = 'ANGLE'
    tc.apply_modifiers(body)
    hull_source = body
    # Forklift / drainage slots through the base.
    for y in (-0.8, 0.8):
        cutter = tc.box("slot", (0.8, 0.42, 0.075), (0.0, y, 0.0375 - 0.001))
        b = body.modifiers.new("Slot", 'BOOLEAN')
        b.operation = 'DIFFERENCE'
        b.object = cutter
        b.solver = 'EXACT'
        tc.apply_modifiers(body)
        bpy.data.objects.remove(cutter, do_unlink=True)
    # Lifting anchors on top.
    anchors = []
    for y in (-0.9, 0.9):
        anchors.append(tc.box("anchor", (0.05, 0.12, 0.03), (0.0, y, 0.915), bevel=0.008))
    body = tc.join([body] + anchors, ASSET)
    tc.set_origin_floor_center(body)
    tc.shade_auto_smooth(body, 30.0)
    tc.uv_unwrap(body)
    body.data.materials.append(concrete_material())
    lod1 = tc.make_lod(body, 0.5, f"{ASSET}_LOD1")
    hull = tc.extrude_profile("hull_src", PROFILE, LENGTH)
    collisions = [tc.collision_hull(f"SM_{ASSET}", 0, hull)]
    bpy.data.objects.remove(hull, do_unlink=True)
    return tc.finish_asset(ASSET, body, lod1, collisions, body.data.materials[0],
                           notes="Matches 'barrier' solids (3.0 x 0.6 x 0.9 m). Material: concrete (non-penetrable).")


if __name__ == "__main__":
    build()
