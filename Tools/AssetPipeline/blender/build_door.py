"""Interior wooden door, 1.0 x 0.05 x 2.15 m (the map kit's door slab).

Four recessed panels between stiles and rails, a lever handle on both
sides. blender -b --factory-startup --python Tools/AssetPipeline/blender/build_door.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import tfps_common as tc  # noqa: E402

ASSET = "WoodDoor"
tc.TEX_SIZE = int(os.environ.get("TFPS_TEX_SIZE", "1024"))
W, T, H = 1.0, 0.05, 2.15
WOOD = (0.36, 0.22, 0.12)
PANEL = (0.31, 0.19, 0.10)
BRASS = (0.72, 0.56, 0.28)


def build():
    tc.reset_scene()
    st, rl = 0.12, 0.14   # stile and rail widths
    parts = [
        tc.paint(tc.box("stile_l", (st, T, H), (-W / 2 + st / 2, 0, H / 2), bevel=0.004), WOOD, 0.0, 0.55),
        tc.paint(tc.box("stile_r", (st, T, H), (W / 2 - st / 2, 0, H / 2), bevel=0.004), WOOD, 0.0, 0.55),
    ]
    inner = W - 2 * st
    for name, z in (("rail_b", rl * 1.5 / 2), ("rail_m", 0.95), ("rail_t", H - rl / 2)):
        h = rl * 1.5 if name == "rail_b" else rl
        parts.append(tc.paint(tc.box(name, (inner, T, h), (0, 0, z), bevel=0.004), WOOD, 0.0, 0.55))
    mullion = 0.1
    pw = (inner - mullion) / 2
    for (z0, z1) in ((rl * 1.5, 0.95 - rl / 2), (0.95 + rl / 2, H - rl)):
        parts.append(tc.paint(tc.box(f"mull{z0}", (mullion, T, z1 - z0), (0, 0, (z0 + z1) / 2), bevel=0.004),
                              WOOD, 0.0, 0.55))
        for sx in (-1, 1):
            x = sx * (mullion / 2 + pw / 2)
            # Recessed panel (thinner) with a raised field.
            parts.append(tc.paint(tc.box(f"panel{z0}{sx}", (pw, T * 0.5, z1 - z0), (x, 0, (z0 + z1) / 2)),
                                  PANEL, 0.0, 0.6))
            parts.append(tc.paint(tc.box(f"field{z0}{sx}", (pw - 0.06, T * 0.7, z1 - z0 - 0.06),
                                         (x, 0, (z0 + z1) / 2), bevel=0.008), PANEL, 0.0, 0.6))
    for sy in (-1, 1):
        y = sy * (T / 2 + 0.025)
        parts.append(tc.paint(tc.cylinder(f"rose{sy}", 0.025, 0.01, (W / 2 - 0.07, sy * (T / 2 + 0.005), 1.0), 'Y', 24),
                              BRASS, 1.0, 0.3))
        parts.append(tc.paint(tc.box(f"lever{sy}", (0.12, 0.016, 0.018), (W / 2 - 0.11, y, 1.0), bevel=0.006),
                              BRASS, 1.0, 0.25))
    door = tc.join(parts, ASSET)
    tc.set_origin_floor_center(door)
    tc.shade_auto_smooth(door)
    tc.uv_unwrap(door)
    mat = tc.painted_material("M_WoodDoor_Proc", grain=0.12, bump_strength=0.25)
    door.data.materials.append(mat)
    lod1 = tc.make_lod(door, 0.3, f"{ASSET}_LOD1")
    collisions = [tc.collision_box(f"SM_{ASSET}", 0, (W, T, H), (0, 0, H / 2))]
    return tc.finish_asset(ASSET, door, lod1, collisions, mat,
                           notes="Matches mapkit DOOR_W x DOOR_SLAB x DOOR_H door solids (wood, breakable).")


if __name__ == "__main__":
    build()
