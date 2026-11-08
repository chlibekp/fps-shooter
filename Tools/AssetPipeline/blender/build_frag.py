"""Fragmentation grenade (gadget id 1): segmented body, fuse, lever and pin ring.

blender -b --factory-startup --python Tools/AssetPipeline/blender/build_frag.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import bpy  # noqa: E402
import tfps_common as tc  # noqa: E402

ASSET = "FragGrenade"
GREEN = (0.13, 0.17, 0.09)
STEEL = (0.55, 0.55, 0.57)


def build():
    tc.reset_scene()
    parts = []
    # Body: a squashed sphere with raised fragmentation segments.
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=0.032, location=(0, 0, 0.038))
    body = bpy.context.active_object
    body.scale = (1.0, 1.0, 1.15)
    bpy.ops.object.transform_apply(scale=True)
    parts.append(tc.paint(body, GREEN, 0.0, 0.6))
    for i in range(4):
        z = 0.016 + i * 0.015
        r = 0.033 * math.sqrt(max(0.05, 1.0 - ((z - 0.038) / 0.037) ** 2))
        parts.append(tc.paint(tc.cylinder(f"rib{i}", r + 0.0015, 0.003, (0, 0, z), 'Z', 24), GREEN, 0.0, 0.65))
    # Fuse assembly, spoon (lever) along the body, pull ring.
    parts.append(tc.paint(tc.cylinder("fuse", 0.011, 0.022, (0, 0, 0.083), 'Z', 16), STEEL, 1.0, 0.35))
    # The spoon hugs the body: from the fuse down along the side.
    lever = tc.box("lever", (0.012, 0.004, 0.062), (0.0, -0.0315, 0.058), bevel=0.0015)
    lever.rotation_euler = (math.radians(14.0), 0, 0)
    tc.select_only([lever])
    bpy.ops.object.transform_apply(rotation=True)
    parts.append(tc.paint(lever, STEEL, 1.0, 0.4))
    bpy.ops.mesh.primitive_torus_add(major_radius=0.012, minor_radius=0.0016, location=(0.018, 0, 0.09),
                                     rotation=(0, math.pi / 2, 0))
    ring = bpy.context.active_object
    bpy.ops.object.transform_apply(rotation=True)
    parts.append(tc.paint(ring, STEEL, 1.0, 0.3))
    frag = tc.join(parts, ASSET)
    tc.set_origin_floor_center(frag)
    tc.shade_auto_smooth(frag)
    tc.uv_unwrap(frag)
    mat = tc.painted_material("M_FragGrenade_Proc")
    frag.data.materials.append(mat)
    lod1 = tc.make_lod(frag, 0.3, f"{ASSET}_LOD1")
    collisions = [tc.collision_box(f"SM_{ASSET}", 0, (0.07, 0.07, 0.1), (0, 0, 0.05))]
    return tc.finish_asset(ASSET, frag, lod1, collisions, mat,
                           notes="Gadget 1 (frag). Server collision: sphere of radius 0.06 m.")


if __name__ == "__main__":
    build()
