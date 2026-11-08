"""Building blocks for TacticalFPS map scripts.

All gameplay geometry is axis-aligned boxes in meters (Z-up, X forward,
Y right). Conventions the kit enforces:

* Walls are split into panels of at most PANEL metres. A panel is the unit
  of wall reinforcement (one solid) and keeps destruction cells local.
* Openings (doors, windows) are cut out of walls; their sills and lintels
  are separate solids. Doors become thin breakable wooden slabs, windows get
  glass panes, both centred in the wall's thickness.
* Floors with holes are decomposed into rectangles; a hole can be left open
  or filled with a breakable hatch.
* Stairs are stacks of solid steps rising at most STEP_RISE per step (the
  movement code steps up to 0.35 m).

Scripts call `Map.write()` which validates basic invariants and writes
Shared/Definitions/maps/<name>.json.
"""
import json
import math
import os

PANEL = 2.4
STEP_RISE = 0.25
DOOR_W, DOOR_H = 1.0, 2.15
WIN_SILL, WIN_HEAD = 1.0, 2.1
PANE = 0.02
DOOR_SLAB = 0.05

MATERIALS = {"concrete", "brick", "wood", "drywall", "metal", "glass", "reinforced", "dirt"}


def _r(v):
    return round(v, 4)


class Opening:
    """A hole in a wall: `at`/`width` along the wall, `sill`/`head` above the wall's base."""

    def __init__(self, at, width, sill, head, fill=None):
        self.a0, self.a1 = at - width / 2.0, at + width / 2.0
        self.sill, self.head, self.fill = sill, head, fill


def door(at, width=DOOR_W, height=DOOR_H, fill="door"):
    """fill: 'door' (breakable wooden door), 'barricade' (planks), None (open)."""
    return Opening(at, width, None, height, fill)


def window(at, width=1.4, sill=WIN_SILL, head=WIN_HEAD, glass=True):
    return Opening(at, width, sill, head, "glass" if glass else None)


class Map:
    def __init__(self, name, kill_z=-30.0):
        self.name, self.kill_z = name, kill_z
        self.solids, self.spawns, self.objectives = [], [], []

    # -- primitives ------------------------------------------------------
    def box(self, x0, y0, z0, x1, y1, z1, material, tag, destructible=None):
        assert material in MATERIALS, material
        lo = [min(x0, x1), min(y0, y1), min(z0, z1)]
        hi = [max(x0, x1), max(y0, y1), max(z0, z1)]
        if any(h - l < 1e-4 for l, h in zip(lo, hi)):
            return
        s = {"min": [_r(v) for v in lo], "max": [_r(v) for v in hi], "material": material, "tag": tag}
        if destructible is not None:
            s["destructible"] = destructible
        self.solids.append(s)

    def _wall_box(self, axis, t0, t1, a0, a1, z0, z1, material, tag, destructible):
        if axis == "x":
            self.box(a0, t0, z0, a1, t1, z1, material, tag, destructible)
        else:
            self.box(t0, a0, z0, t1, a1, z1, material, tag, destructible)

    def _panels(self, axis, t0, t1, a0, a1, z0, z1, material, tag, destructible):
        n = max(1, math.ceil((a1 - a0) / PANEL - 1e-6))
        for i in range(n):
            self._wall_box(axis, t0, t1, a0 + (a1 - a0) * i / n, a0 + (a1 - a0) * (i + 1) / n,
                           z0, z1, material, tag, destructible)

    def wall(self, axis, t0, t1, a0, a1, z0, z1, material, tag, openings=(), destructible=None):
        """Wall running along `axis` ('x' or 'y') from a0 to a1, thickness t0..t1
        (the other horizontal axis), height z0..z1, with openings cut out."""
        a = a0
        for o in sorted(openings, key=lambda o: o.a0):
            assert a0 <= o.a0 and o.a1 <= a1 and o.a0 >= a - 1e-6, (tag, o.a0, o.a1)
            self._panels(axis, t0, t1, a, o.a0, z0, z1, material, tag, destructible)
            # Sill and head are measured from the wall's base.
            sill = z0 + (o.sill if o.sill is not None else 0.0)
            head = z0 + o.head
            if sill > z0:
                self._wall_box(axis, t0, t1, o.a0, o.a1, z0, sill, material, tag + "_sill", destructible)
            if head < z1:
                self._wall_box(axis, t0, t1, o.a0, o.a1, head, z1, material, tag + "_lintel", destructible)
            mid = (t0 + t1) / 2.0
            if o.fill == "door":
                self._wall_box(axis, mid - DOOR_SLAB / 2, mid + DOOR_SLAB / 2, o.a0, o.a1, sill, head,
                               "wood", "door", None)
            elif o.fill == "glass":
                self._wall_box(axis, mid - PANE / 2, mid + PANE / 2, o.a0, o.a1, sill, head,
                               "glass", "window_glass", None)
            elif o.fill == "barricade":
                h = head - sill
                for k in range(4):
                    zb = sill + h * (0.12 + 0.22 * k)
                    self._wall_box(axis, mid - 0.03, mid + 0.03, o.a0, o.a1, zb, zb + h * 0.1,
                                   "wood", "barricade", None)
            a = o.a1
        self._panels(axis, t0, t1, a, a1, z0, z1, material, tag, destructible)

    def floor(self, x0, y0, x1, y1, z_top, thickness, material, tag, holes=(), destructible=None):
        """Slab x0..x1, y0..y1 with its top at z_top. holes: (hx0, hy0, hx1, hy1, fill_material|None)."""
        xs = sorted({x0, x1} | {h[0] for h in holes} | {h[2] for h in holes})
        ys = sorted({y0, y1} | {h[1] for h in holes} | {h[3] for h in holes})

        def in_hole(cx, cy):
            return any(h[0] < cx < h[2] and h[1] < cy < h[3] for h in holes)

        z0 = z_top - thickness
        for i in range(len(xs) - 1):
            # Merge consecutive y cells of the strip that are not holes.
            j = 0
            while j < len(ys) - 1:
                cx, cy = (xs[i] + xs[i + 1]) / 2, (ys[j] + ys[j + 1]) / 2
                if in_hole(cx, cy):
                    j += 1
                    continue
                k = j
                while k + 1 < len(ys) - 1 and not in_hole(cx, (ys[k + 1] + ys[k + 2]) / 2):
                    k += 1
                self.box(xs[i], ys[j], z0, xs[i + 1], ys[k + 1], z_top, material, tag, destructible)
                j = k + 1
        for h in holes:
            if h[4]:
                self.box(h[0], h[1], z0, h[2], h[3], z_top, h[4], "floor_hatch")

    def stairs(self, x0, y0, x1, y1, z0, z1, rise_dir, material="concrete", tag="stairs"):
        """Solid stairs in the footprint, rising toward rise_dir ('+x','-x','+y','-y')."""
        n = max(1, math.ceil((z1 - z0) / STEP_RISE - 1e-6))
        for i in range(n):
            top = z0 + (z1 - z0) * (i + 1) / n
            f0, f1 = i / n, 1.0
            if rise_dir == "+x":
                self.box(x0 + (x1 - x0) * f0, y0, z0, x1, y1, top, material, tag, False)
            elif rise_dir == "-x":
                self.box(x0, y0, z0, x1 - (x1 - x0) * f0, y1, top, material, tag, False)
            elif rise_dir == "+y":
                self.box(x0, y0 + (y1 - y0) * f0, z0, x1, y1, top, material, tag, False)
            else:
                self.box(x0, y0, z0, x1, y1 - (y1 - y0) * f0, top, material, tag, False)

    def crate(self, cx, cy, size=1.2, z=0.0, material="wood"):
        self.box(cx - size / 2, cy - size / 2, z, cx + size / 2, cy + size / 2, z + size, material, "crate")

    def container(self, x0, y0, along_x=True, z=0.0, open_end=True):
        """6 x 2.4 x 2.6 m shipping container (metal), optionally open at the far end."""
        L, W, H, t = 6.0, 2.4, 2.6, 0.1
        if along_x:
            x1, y1 = x0 + L, y0 + W
            self.box(x0, y0, z, x1, y0 + t, z + H, "metal", "container")
            self.box(x0, y1 - t, z, x1, y1, z + H, "metal", "container")
            self.box(x0, y0, z + H - t, x1, y1, z + H, "metal", "container")
            self.box(x0, y0, z, x1, y1, z + 0.05, "metal", "container")
            if not open_end:
                self.box(x1 - t, y0, z, x1, y1, z + H, "metal", "container")
            self.box(x0, y0, z, x0 + t, y1, z + H, "metal", "container")
        else:
            x1, y1 = x0 + W, y0 + L
            self.box(x0, y0, z, x0 + t, y1, z + H, "metal", "container")
            self.box(x1 - t, y0, z, x1, y1, z + H, "metal", "container")
            self.box(x0, y0, z + H - t, x1, y1, z + H, "metal", "container")
            self.box(x0, y0, z, x1, y1, z + 0.05, "metal", "container")
            if not open_end:
                self.box(x0, y1 - t, z, x1, y1, z + H, "metal", "container")
            self.box(x0, y0, z, x1, y0 + t, z + H, "metal", "container")

    def spawn(self, x, y, z, yaw, team):
        self.spawns.append({"pos": [_r(x), _r(y), _r(z)], "yaw": yaw, "team": team})

    def objective(self, name, x0, y0, z0, x1, y1, z1):
        self.objectives.append({"name": name, "min": [x0, y0, z0], "max": [x1, y1, z1]})

    # -- output ---------------------------------------------------------
    def write(self):
        assert len(self.solids) <= 4096
        teams = {s["team"] for s in self.spawns}
        assert teams == {0, 1}, "both teams need spawns"
        doc = {"name": self.name, "kill_z": self.kill_z, "solids": self.solids,
               "spawns": self.spawns, "objectives": self.objectives}
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Shared", "Definitions",
                           "maps", self.name + ".json")
        with open(out, "w", newline="\n") as f:
            f.write(json.dumps(doc, indent=1) + "\n")
        print(f"wrote {os.path.normpath(out)}: {len(self.solids)} solids, {len(self.spawns)} spawns, "
              f"{len(self.objectives)} objectives")
