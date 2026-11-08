#!/usr/bin/env python3
"""Builds all Blender assets headlessly.

Usage: python3 Tools/AssetPipeline/build_assets.py [asset ...]
       assets: crate, barrier, container, drone, breach_charge, frag, camera, door,
               window, brick_panel (default: all)
Env:   BLENDER=/path/to/blender  TFPS_TEX_SIZE=512  TFPS_BAKE_SAMPLES=16
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = {
    "crate": "build_crate.py",
    "barrier": "build_barrier.py",
    "container": "build_container.py",
    "drone": "build_drone.py",
    "breach_charge": "build_breach_charge.py",
    "frag": "build_frag.py",
    "camera": "build_camera.py",
    "door": "build_door.py",
    "window": "build_window.py",
    "brick_panel": "build_brick_panel.py",
}


def main():
    blender = os.environ.get("BLENDER") or shutil.which("blender")
    if not blender:
        sys.exit("Blender not found: install Blender 4.x or set BLENDER=/path/to/blender")
    names = sys.argv[1:] or list(SCRIPTS)
    for name in names:
        if name not in SCRIPTS:
            sys.exit(f"unknown asset '{name}', expected one of {', '.join(SCRIPTS)}")
        script = os.path.join(HERE, "blender", SCRIPTS[name])
        print(f"== {name}")
        proc = subprocess.run([blender, "-b", "--factory-startup", "--python-exit-code", "1", "--python", script],
                              capture_output=True, text=True)
        done = [l for l in proc.stdout.splitlines() if l.startswith("TFPS_ASSET_DONE")]
        if proc.returncode != 0 or not done:
            print(proc.stdout[-4000:])
            print(proc.stderr[-4000:])
            sys.exit(f"building {name} failed")
        print(done[-1])


if __name__ == "__main__":
    main()
