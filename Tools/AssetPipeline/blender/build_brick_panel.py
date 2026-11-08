"""Brick wall panel, 2.4 x 0.3 x 3.0 m — one map-kit wall panel (the unit of
reinforcement). Running-bond bricks with recessed mortar, baked to tiling
textures. blender -b --factory-startup --python Tools/AssetPipeline/blender/build_brick_panel.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import tfps_common as tc  # noqa: E402

ASSET = "BrickWallPanel"
tc.TEX_SIZE = int(os.environ.get("TFPS_TEX_SIZE", "1024"))
W, D, H = 2.4, 0.3, 3.0


def brick_material():
    m, nt, bsdf, coord = tc.new_material("M_Brick_Proc")
    mapping = tc.node(nt, "ShaderNodeMapping")
    nt.links.new(coord.outputs["Object"], mapping.inputs["Vector"])
    # Brick Texture works in the XY plane: map the wall's X/Z onto it.
    sep = tc.node(nt, "ShaderNodeSeparateXYZ")
    nt.links.new(coord.outputs["Object"], sep.inputs["Vector"])
    comb = tc.node(nt, "ShaderNodeCombineXYZ")
    nt.links.new(sep.outputs["X"], comb.inputs["X"])
    nt.links.new(sep.outputs["Z"], comb.inputs["Y"])
    nt.links.new(sep.outputs["Y"], comb.inputs["Z"])
    brick = tc.node(nt, "ShaderNodeTexBrick")
    brick.inputs["Scale"].default_value = 1.0
    brick.inputs["Brick Width"].default_value = 0.215
    brick.inputs["Row Height"].default_value = 0.075
    brick.inputs["Mortar Size"].default_value = 0.01
    brick.inputs["Color1"].default_value = (0.36, 0.13, 0.08, 1)
    brick.inputs["Color2"].default_value = (0.45, 0.19, 0.11, 1)
    brick.inputs["Mortar"].default_value = (0.55, 0.53, 0.5, 1)
    nt.links.new(comb.outputs["Vector"], brick.inputs["Vector"])
    noise = tc.node(nt, "ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 25.0
    noise.inputs["Detail"].default_value = 10.0
    nt.links.new(coord.outputs["Object"], noise.inputs["Vector"])
    vary = tc.node(nt, "ShaderNodeMix", data_type='RGBA', blend_type='MULTIPLY')
    vary.inputs["Factor"].default_value = 0.35
    nt.links.new(brick.outputs["Color"], vary.inputs[6])
    nt.links.new(noise.outputs["Color"], vary.inputs[7])
    nt.links.new(vary.outputs[2], bsdf.inputs["Base Color"])
    rough = tc.node(nt, "ShaderNodeMapRange")
    rough.inputs["To Min"].default_value = 0.92
    rough.inputs["To Max"].default_value = 0.78
    nt.links.new(brick.outputs["Fac"], rough.inputs["Value"])
    nt.links.new(rough.outputs["Result"], bsdf.inputs["Roughness"])
    bump = tc.node(nt, "ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.6
    bump.inputs["Distance"].default_value = 0.01
    inv = tc.node(nt, "ShaderNodeMath", operation='SUBTRACT')
    inv.inputs[0].default_value = 1.0
    nt.links.new(brick.outputs["Fac"], inv.inputs[1])
    nt.links.new(inv.outputs["Value"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    m["tfps_metallic"] = 0.0
    return m


def build():
    tc.reset_scene()
    wall = tc.box(ASSET, (W, D, H), (0, 0, H / 2), bevel=0.01)
    tc.set_origin_floor_center(wall)
    tc.shade_auto_smooth(wall)
    tc.uv_unwrap(wall)
    mat = brick_material()
    wall.data.materials.append(mat)
    lod1 = tc.make_lod(wall, 0.5, f"{ASSET}_LOD1")
    collisions = [tc.collision_box(f"SM_{ASSET}", 0, (W, D, H), (0, 0, H / 2))]
    return tc.finish_asset(ASSET, wall, lod1, collisions, mat,
                           notes="One mapkit wall panel (PANEL = 2.4 m). Visual only: gameplay uses the cell grid.")


if __name__ == "__main__":
    build()
