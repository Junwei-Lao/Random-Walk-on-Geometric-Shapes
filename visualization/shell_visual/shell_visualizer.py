"""
Scatter-plots a lattice point-set from a text file: one point per line,
either "x y" (2D) or "x y z" (3D). Matches the plain-text format written by
tools/generate_mask_2d.cpp, tools/generate_mask_3d.cpp (mask/shell dumps),
and the convex-hull engines' --export-points output.

Consolidates two previously separate scripts -- genDots.py (2D scatter) and
3dplotting.py (3D scatter) -- into one file that auto-detects dimensionality
from the number of columns in the input, so this is the one file in
shell_visual/.

All C++ engines/tools save their output into data/ at the repo root (see the
makefile's OUTDIR / each binary's --outdir default). DATA_DIR below is
computed relative to this script's own location, so it resolves correctly
no matter what directory you run the script from.
"""

from pathlib import Path

import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401 -- registers the 3D projection

# repo_root/visualization/shell_visual/shell_visualizer.py -> repo_root/data
DATA_DIR = Path(__file__).resolve().parent.parent.parent / "data"


# =============================
# Configuration -- edit as needed
# =============================
# Matches the C++ naming convention mask_{SHAPE}_{INDEX}[_shell].txt, e.g.
# the file produced by `make mask-2d SHAPE=HEXAGON INDEX=50 SHELL_ONLY=1`.
FILENAME = DATA_DIR / "mask_POLYGON15_50_shell.txt"


def read_points(filename: str):
    points = []
    with open(filename, "r") as f:
        for line_num, line in enumerate(f, 1):
            parts = line.strip().split()
            if not parts or parts[0].startswith("#"):
                continue
            if len(parts) not in (2, 3):
                print(f"Skipping line {line_num}: expected 2 or 3 columns, got {len(parts)}")
                continue
            try:
                points.append(tuple(map(float, parts)))
            except ValueError:
                print(f"Skipping line {line_num}: non-numeric data")
    return points


def plot_2d(points, title: str):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]

    plt.scatter(xs, ys, s=6)
    plt.xlabel("x")
    plt.ylabel("y")
    plt.grid(True)
    plt.gca().set_aspect("equal", adjustable="box")
    plt.title(title)
    plt.show()


def plot_3d(points, title: str):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    zs = [p[2] for p in points]

    fig = plt.figure()
    ax = fig.add_subplot(111, projection="3d")
    ax.scatter(xs, ys, zs, s=6)
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.set_zlabel("Z")
    ax.set_title(title)
    plt.show()


def main():
    points = read_points(FILENAME)
    if not points:
        print("No valid points found.")
        return

    print(f"Loaded {len(points)} points from {FILENAME}.")
    title = f"{len(points)} points ({FILENAME})"

    dim = len(points[0])
    if dim == 2:
        plot_2d(points, title)
    elif dim == 3:
        plot_3d(points, title)
    else:
        print(f"Unsupported point dimensionality: {dim}")


if __name__ == "__main__":
    main()
