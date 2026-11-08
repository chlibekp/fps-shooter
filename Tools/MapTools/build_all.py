#!/usr/bin/env python3
"""Regenerates every map JSON from its script.

Usage: python3 Tools/MapTools/build_all.py
CI runs this and fails if the committed JSON differs (edit scripts, not JSON).
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = ["build_test_arena.py", "build_harbor.py", "build_villa.py", "build_consulate.py"]

for s in SCRIPTS:
    subprocess.run([sys.executable, os.path.join(HERE, s)], check=True, cwd=HERE)
