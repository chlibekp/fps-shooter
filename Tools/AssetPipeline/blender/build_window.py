"""Window frame, 1.4 x 0.3 x 1.1 m opening (the map kit's default window).

Aluminium frame with a centre mullion and an outside sill; the glass pane
is separate (breakable gameplay glass). blender -b --factory-startup
--python Tools/AssetPipeline/blender/build_window.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import tfps_common as tc  # noqa: E402

ASSET = "WindowFrame"
W, D, H = 1.4, 0.3, 1.1
ALU = (0.78, 0.79, 0.8)
STONE = (0.55, 0.53, 0.5)


def build():
    tc.reset_scene()
    f, fd = 0.06, 0.08   # frame width and depth
    parts = [
        tc.paint(tc.box("head", (W, fd, f), (0, 0, H - f / 2), bevel=0.004), ALU, 1.0, 0.35),
        tc.paint(tc.box("bottom", (W, fd, f), (0, 0, f / 2), bevel=0.004), ALU, 1.0, 0.35),
        tc.paint(tc.box("jamb_l", (f, fd, H), (-W / 2 + f / 2, 0, H / 2), bevel=0.004), ALU, 1.0, 0.35),
        tc.paint(tc.box("jamb_r", (f, fd, H), (W / 2 - f / 2, 0, H / 2), bevel=0.004), ALU, 1.0, 0.35),
        tc.paint(tc.box("mullion", (0.04, fd, H - 2 * f), (0, 0, H / 2), bevel=0.003), ALU, 1.0, 0.35),
        # Stone sill projecting outside (-Y) and an inside board (+Y).
        tc.paint(tc.box("sill", (W + 0.1, D / 2 + 0.05, 0.05), (0, -D / 4 - 0.025, -0.025), bevel=0.006), STONE, 0.0, 0.8),
        tc.paint(tc.box("board", (W, D / 2, 0.025), (0, D / 4, -0.0125), bevel=0.004), (0.85, 0.84, 0.8), 0.0, 0.5),
    ]
    win = tc.join(parts, ASSET)
    tc.set_origin_floor_center(win)
    tc.shade_auto_smooth(win)
    tc.uv_unwrap(win)
    mat = tc.painted_material("M_WindowFrame_Proc")
    win.data.materials.append(mat)
    lod1 = tc.make_lod(win, 0.5, f"{ASSET}_LOD1")
    collisions = [tc.collision_box(f"SM_{ASSET}", 0, (W, fd, f), (0, 0, f / 2 + 0.05))]
    return tc.finish_asset(ASSET, win, lod1, collisions, mat,
                           notes="Frames the mapkit default window (1.4 x 1.1 m, sill 1.0 m); glass is gameplay geometry.")


if __name__ == "__main__":
    build()
