"""Wall camera (gadget id 6): wall plate, swivel arm and dome housing.

The plate sits against the wall at +Y; the camera looks along -Y.
blender -b --factory-startup --python Tools/AssetPipeline/blender/build_camera.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import bpy  # noqa: E402
import tfps_common as tc  # noqa: E402

ASSET = "WallCamera"
WHITE = (0.75, 0.76, 0.74)
DARK = (0.03, 0.03, 0.035)


def build():
    tc.reset_scene()
    parts = [
        tc.paint(tc.box("plate", (0.1, 0.012, 0.12), (0, 0.054, 0.06), bevel=0.004), WHITE, 0.0, 0.45),
        tc.paint(tc.cylinder("arm", 0.012, 0.05, (0, 0.03, 0.06), 'Y', 16), WHITE, 0.0, 0.4),
    ]
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=0.04, location=(0, -0.01, 0.06))
    dome = bpy.context.active_object
    parts.append(tc.paint(dome, WHITE, 0.0, 0.35))
    parts.append(tc.paint(tc.cylinder("lens_ring", 0.022, 0.012, (0, -0.046, 0.06), 'Y', 24), DARK, 1.0, 0.3))
    parts.append(tc.paint(tc.cylinder("lens", 0.015, 0.006, (0, -0.053, 0.06), 'Y', 24), (0.02, 0.02, 0.05), 0.0, 0.04))
    cam = tc.join(parts, ASSET)
    cam.rotation_euler = (math.radians(-10.0), 0, 0)  # tilted slightly down
    tc.select_only([cam])
    bpy.ops.object.transform_apply(rotation=True)
    tc.set_origin_floor_center(cam)
    tc.shade_auto_smooth(cam)
    tc.uv_unwrap(cam)
    mat = tc.painted_material("M_WallCamera_Proc")
    cam.data.materials.append(mat)
    lod1 = tc.make_lod(cam, 0.35, f"{ASSET}_LOD1")
    collisions = [tc.collision_box(f"SM_{ASSET}", 0, (0.1, 0.12, 0.12), (0, 0, 0.06))]
    return tc.finish_asset(ASSET, cam, lod1, collisions, mat,
                           notes="Gadget 6 (camera). Server hit box: 0.16 m cube around the placement point.")


if __name__ == "__main__":
    build()
