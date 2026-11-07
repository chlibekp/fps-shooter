"""Builds every asset. Usage (from the repo root):
    python3 Tools/AssetPipeline/build_assets.py
or directly:
    blender -b --factory-startup --python Tools/AssetPipeline/blender/build_all.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import build_barrier  # noqa: E402
import build_container  # noqa: E402
import build_crate  # noqa: E402

for mod in (build_crate, build_barrier, build_container):
    mod.build()
