"""Breaching charge (gadget id 4): frame charge with a detonator, 0.24 x 0.05 x 0.34 m.

Front faces -Y (away from the wall it sticks to).
blender -b --factory-startup --python Tools/AssetPipeline/blender/build_breach_charge.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import tfps_common as tc  # noqa: E402

ASSET = "BreachCharge"
OLIVE = (0.17, 0.19, 0.11)
TAN = (0.52, 0.44, 0.30)
DARK = (0.06, 0.06, 0.065)
STEEL = (0.5, 0.5, 0.52)


def build():
    tc.reset_scene()
    parts = [
        tc.paint(tc.box("backplate", (0.24, 0.012, 0.34), (0, 0.019, 0.17), bevel=0.004), OLIVE, 0.0, 0.6),
        tc.paint(tc.box("charge", (0.2, 0.026, 0.3), (0, 0.0, 0.17), bevel=0.006), TAN, 0.0, 0.75),
        tc.paint(tc.box("det", (0.07, 0.03, 0.09), (0.0, -0.025, 0.24), bevel=0.006), DARK, 0.0, 0.45),
        tc.paint(tc.box("led", (0.012, 0.006, 0.012), (0.02, -0.042, 0.265)), (0.9, 0.05, 0.03), 0.0, 0.2),
    ]
    # Straps and adhesive feet.
    for z in (0.07, 0.27):
        parts.append(tc.paint(tc.box(f"strap{z}", (0.22, 0.03, 0.025), (0, -0.002, z)), DARK, 0.0, 0.7))
    for (x, z) in ((-0.1, 0.03), (0.1, 0.03), (-0.1, 0.31), (0.1, 0.31)):
        parts.append(tc.paint(tc.cylinder(f"foot{x}{z}", 0.012, 0.01, (x, 0.03, z), 'Y', 12), STEEL, 1.0, 0.35))
    parts.append(tc.paint(tc.cylinder("wire", 0.003, 0.12, (0.0, -0.016, 0.17), 'Z', 8), (0.6, 0.08, 0.05), 0.0, 0.5))
    charge = tc.join(parts, ASSET)
    tc.set_origin_floor_center(charge)
    tc.shade_auto_smooth(charge)
    tc.uv_unwrap(charge)
    mat = tc.painted_material("M_BreachCharge_Proc")
    charge.data.materials.append(mat)
    lod1 = tc.make_lod(charge, 0.4, f"{ASSET}_LOD1")
    collisions = [tc.collision_box(f"SM_{ASSET}", 0, (0.24, 0.07, 0.34), (0, 0, 0.17))]
    return tc.finish_asset(ASSET, charge, lod1, collisions, mat,
                           notes="Gadget 4 (breaching charge). Placed 3 cm off the surface by the server.")


if __name__ == "__main__":
    build()
