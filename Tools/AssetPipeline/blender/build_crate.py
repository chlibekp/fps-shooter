"""Wooden shipping crate, 1.2 m cube: planked faces, edge battens, diagonal braces.

blender -b --factory-startup --python Tools/AssetPipeline/blender/build_crate.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import bpy  # noqa: E402
import tfps_common as tc  # noqa: E402

ASSET = "WoodCrate"
# Close-up prop: higher texel density than the default.
tc.TEX_SIZE = int(os.environ.get("TFPS_TEX_SIZE", "1024"))
S = 1.2           # outer size (m)
T = 0.022         # plank thickness
BAT = 0.09        # batten width
BAT_T = 0.028     # batten thickness
PLANKS = 5
GAP = 0.006


def wood_material():
    m, nt, bsdf, coord = tc.new_material("M_WoodCrate_Proc")
    mapping = tc.node(nt, "ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (0.35, 0.35, 2.2)  # long grain along the planks
    nt.links.new(coord.outputs["Object"], mapping.inputs["Vector"])
    wave = tc.node(nt, "ShaderNodeTexWave", wave_type='BANDS', bands_direction='Z')
    wave.inputs["Scale"].default_value = 9.0
    wave.inputs["Distortion"].default_value = 7.0
    wave.inputs["Detail"].default_value = 6.0
    wave.inputs["Detail Roughness"].default_value = 0.65
    nt.links.new(mapping.outputs["Vector"], wave.inputs["Vector"])
    noise = tc.node(nt, "ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 3.0
    noise.inputs["Detail"].default_value = 8.0
    nt.links.new(coord.outputs["Object"], noise.inputs["Vector"])
    mix = tc.node(nt, "ShaderNodeMix", data_type='FLOAT')
    mix.inputs["Factor"].default_value = 0.35
    nt.links.new(wave.outputs["Fac"], mix.inputs["A"])
    nt.links.new(noise.outputs["Fac"], mix.inputs["B"])
    col = tc.ramp(nt, [
        (0.0, (0.16, 0.09, 0.04, 1.0)),
        (0.45, (0.33, 0.20, 0.10, 1.0)),
        (0.75, (0.45, 0.30, 0.16, 1.0)),
        (1.0, (0.52, 0.37, 0.21, 1.0)),
    ])
    nt.links.new(mix.outputs["Result"], col.inputs["Fac"])
    # Weathering: darker grime toward the bottom.
    sep = tc.node(nt, "ShaderNodeSeparateXYZ")
    nt.links.new(coord.outputs["Object"], sep.inputs["Vector"])
    grime = tc.node(nt, "ShaderNodeMapRange")
    grime.inputs["From Min"].default_value = 0.0
    grime.inputs["From Max"].default_value = 0.35
    grime.inputs["To Min"].default_value = 0.55
    grime.inputs["To Max"].default_value = 1.0
    nt.links.new(sep.outputs["Z"], grime.inputs["Value"])
    mul = tc.node(nt, "ShaderNodeMix", data_type='RGBA', blend_type='MULTIPLY')
    mul.inputs["Factor"].default_value = 1.0
    nt.links.new(col.outputs["Color"], mul.inputs[6])
    gray = tc.node(nt, "ShaderNodeCombineColor")
    nt.links.new(grime.outputs["Result"], gray.inputs["Red"])
    nt.links.new(grime.outputs["Result"], gray.inputs["Green"])
    nt.links.new(grime.outputs["Result"], gray.inputs["Blue"])
    nt.links.new(gray.outputs["Color"], mul.inputs[7])
    nt.links.new(mul.outputs[2], bsdf.inputs["Base Color"])
    rough = tc.node(nt, "ShaderNodeMapRange")
    rough.inputs["To Min"].default_value = 0.62
    rough.inputs["To Max"].default_value = 0.86
    nt.links.new(noise.outputs["Fac"], rough.inputs["Value"])
    nt.links.new(rough.outputs["Result"], bsdf.inputs["Roughness"])
    bump = tc.node(nt, "ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.35
    bump.inputs["Distance"].default_value = 0.002
    nt.links.new(wave.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    m["tfps_metallic"] = 0.0
    return m


def planked_face(name, axis, sign):
    """Planks on the face whose outward normal is `axis` (0=x, 1=y, 2=z) * sign.

    Side faces get horizontal planks stacked vertically; top/bottom planks run
    along X and are stacked along Y.
    """
    inner = S - 2 * BAT_T
    width = (inner - (PLANKS - 1) * GAP) / PLANKS
    if axis == 2:
        run, cross = 0, 1
    else:
        run, cross = 1 - axis, 2
    centre = [0.0, 0.0, S / 2]
    parts = []
    for i in range(PLANKS):
        size = [0.0, 0.0, 0.0]
        loc = list(centre)
        size[axis] = T
        loc[axis] = centre[axis] + sign * (S / 2 - BAT_T - T / 2)
        size[run] = inner
        size[cross] = width
        loc[cross] = centre[cross] - inner / 2 + width / 2 + i * (width + GAP)
        parts.append(tc.box(f"{name}_p{i}", tuple(size), tuple(loc), bevel=0.003))
    return parts


def build():
    tc.reset_scene()
    parts = []
    for axis in range(3):
        for sign in (-1, 1):
            parts += planked_face(f"face{axis}{sign}", axis, sign)
    # Edge battens: 12 edges of the cube.
    h = S / 2 - BAT_T / 2
    for ax in range(3):
        for s1 in (-1, 1):
            for s2 in (-1, 1):
                size = [BAT_T + 0.002, BAT_T + 0.002, BAT_T + 0.002]
                size[ax] = S
                loc = [0.0, 0.0, 0.0]
                others = [a for a in range(3) if a != ax]
                loc[others[0]] = s1 * h
                loc[others[1]] = s2 * h
                loc[2] += S / 2
                b = tc.box(f"edge{ax}{s1}{s2}", tuple(size), tuple(loc), bevel=0.004)
                parts.append(b)
    # Face battens (frame) and diagonal braces on the four sides.
    for ax in (0, 1):
        for sign in (-1, 1):
            plane = sign * (S / 2 + BAT_T / 2 - 0.004)
            along = 1 - ax
            for zc in (BAT / 2 + 0.01, S - BAT / 2 - 0.01):
                size = [0, 0, 0]
                size[ax] = BAT_T
                size[along] = S - 0.02
                size[2] = BAT
                loc = [0, 0, zc]
                loc[ax] = plane
                parts.append(tc.box(f"rail{ax}{sign}{zc:.2f}", tuple(size), tuple(loc), bevel=0.004))
            for side in (-1, 1):
                size = [0, 0, 0]
                size[ax] = BAT_T
                size[along] = BAT
                size[2] = S - 2 * BAT - 0.02
                loc = [0, 0, S / 2]
                loc[ax] = plane
                loc[along] = side * (S / 2 - BAT / 2 - 0.01)
                parts.append(tc.box(f"stile{ax}{sign}{side}", tuple(size), tuple(loc), bevel=0.004))
            diag_len = math.hypot(S - 2 * BAT, S - 2 * BAT) - 0.02
            size = [0, 0, 0]
            size[ax] = BAT_T * 0.9
            size[along] = BAT * 0.9
            size[2] = diag_len
            loc = [0, 0, S / 2]
            loc[ax] = plane
            d = tc.box(f"brace{ax}{sign}", tuple(size), tuple(loc), bevel=0.004)
            angle = math.radians(45.0) * (1 if sign > 0 else -1)
            if ax == 0:
                d.rotation_euler = (angle, 0, 0)
            else:
                d.rotation_euler = (0, angle, 0)
            tc.select_only([d])
            bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
            parts.append(d)
    crate = tc.join(parts, ASSET)
    tc.set_origin_floor_center(crate)
    tc.shade_auto_smooth(crate)
    tc.uv_unwrap(crate)
    crate.data.materials.append(wood_material())
    lod1 = tc.make_lod(crate, 0.35, f"{ASSET}_LOD1")
    collisions = [tc.collision_box(f"SM_{ASSET}", 0, (S, S, S), (0, 0, S / 2))]
    return tc.finish_asset(ASSET, crate, lod1, collisions, crate.data.materials[0],
                           notes="Matches 'crate' solids (1.2 m) in test_arena. Material: wood (penetrable).")


if __name__ == "__main__":
    build()
