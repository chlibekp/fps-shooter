"""Recon drone (gadget id 5): two-wheeled throwable scout, 0.30 x 0.24 x 0.13 m.

blender -b --factory-startup --python Tools/AssetPipeline/blender/build_drone.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import tfps_common as tc  # noqa: E402

ASSET = "ReconDrone"
tc.TEX_SIZE = int(os.environ.get("TFPS_TEX_SIZE", "512"))
POLYMER = (0.05, 0.055, 0.06)
RUBBER = (0.02, 0.02, 0.02)
STEEL = (0.55, 0.56, 0.58)
ACCENT = (0.75, 0.42, 0.06)


def build():
    tc.reset_scene()
    parts = []
    # Chassis: a low body between the wheels, with a bevelled top shell.
    parts.append(tc.paint(tc.box("body", (0.17, 0.11, 0.06), (0, 0, 0.065), bevel=0.012), POLYMER, 0.0, 0.55))
    parts.append(tc.paint(tc.box("shell", (0.14, 0.09, 0.025), (0, 0.005, 0.105), bevel=0.01), POLYMER, 0.0, 0.42))
    parts.append(tc.paint(tc.box("stripe", (0.172, 0.02, 0.012), (0, -0.04, 0.09)), ACCENT, 0.0, 0.5))
    # Wheels with steel hubs.
    for sx in (-1, 1):
        x = sx * 0.115
        parts.append(tc.paint(tc.cylinder(f"wheel{sx}", 0.065, 0.045, (x, 0, 0.065), 'X', 32, bevel=0.008),
                              RUBBER, 0.0, 0.85))
        parts.append(tc.paint(tc.cylinder(f"hub{sx}", 0.03, 0.05, (x, 0, 0.065), 'X', 16), STEEL, 1.0, 0.3))
    # Camera pod on the front (-Y) with a glossy lens, antenna at the back.
    parts.append(tc.paint(tc.cylinder("pod", 0.022, 0.03, (0, -0.06, 0.075), 'Y', 24), STEEL, 1.0, 0.35))
    parts.append(tc.paint(tc.cylinder("lens", 0.014, 0.008, (0, -0.077, 0.075), 'Y', 24), (0.01, 0.01, 0.02), 0.0, 0.05))
    parts.append(tc.paint(tc.cylinder("antenna", 0.003, 0.09, (0.05, 0.045, 0.16), 'Z', 8), RUBBER, 0.0, 0.6))
    drone = tc.join(parts, ASSET)
    tc.set_origin_floor_center(drone)
    tc.shade_auto_smooth(drone)
    tc.uv_unwrap(drone)
    mat = tc.painted_material("M_ReconDrone_Proc")
    drone.data.materials.append(mat)
    lod1 = tc.make_lod(drone, 0.4, f"{ASSET}_LOD1")
    collisions = [tc.collision_box(f"SM_{ASSET}", 0, (0.30, 0.13, 0.13), (0, 0, 0.065))]
    return tc.finish_asset(ASSET, drone, lod1, collisions, mat,
                           notes="Gadget 5 (drone). Server hit box: 0.30 x 0.30 x 0.18 m around the centre.")


if __name__ == "__main__":
    build()
