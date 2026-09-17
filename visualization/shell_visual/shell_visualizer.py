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

Two ways to run this:
  python3 shell_visualizer.py            Interactive: plots FILENAME in a window.
  python3 shell_visualizer.py --all      Batch: renders every mask_*.txt in
                                          DATA_DIR to a same-named .png next
                                          to it, no window required (e.g.
                                          after `make mask-2d SHAPES=ALL
                                          SHELL_ONLY=1` / `make mask-3d
                                          SHAPES3D=ALL SHELL_ONLY=1`).
"""

import sys
from pathlib import Path

import matplotlib

if "--all" in sys.argv:
    matplotlib.use("Agg")  # headless: batch mode never opens a window

import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401 -- registers the 3D projection

# repo_root/visualization/shell_visual/shell_visualizer.py -> repo_root/data
DATA_DIR = Path(__file__).resolve().parent.parent.parent / "data"


# =============================
# Configuration -- edit as needed (interactive single-file mode only)
# =============================
# Matches the C++ naming convention mask_{SHAPE}_{INDEX}[_shell].txt, e.g.
# the file produced by `make mask-2d SHAPE=HEXAGON INDEX=50 SHELL_ONLY=1`.
FILENAME = DATA_DIR / "mask_POLYGON8_50_shell.txt"


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


def plot_2d(points, title: str, out_png: Path = None, show: bool = True):
    fig = plt.figure()
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]

    plt.scatter(xs, ys, s=6)
    plt.xlabel("x")
    plt.ylabel("y")
    plt.grid(True)
    plt.gca().set_aspect("equal", adjustable="box")
    plt.title(title)

    if out_png:
        fig.savefig(out_png, dpi=150, bbox_inches="tight")
    if show:
        plt.show()
    plt.close(fig)


def plot_3d(points, title: str, out_png: Path = None, show: bool = True):
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

    if out_png:
        fig.savefig(out_png, dpi=150, bbox_inches="tight")
    if show:
        plt.show()
    plt.close(fig)


def render_one(filename, out_png: Path = None, show: bool = True):
    points = read_points(filename)
    if not points:
        print(f"No valid points found in {filename}.")
        return

    title = f"{len(points)} points ({Path(filename).name})"
    dim = len(points[0])
    if dim == 2:
        plot_2d(points, title, out_png=out_png, show=show)
    elif dim == 3:
        plot_3d(points, title, out_png=out_png, show=show)
    else:
        print(f"Unsupported point dimensionality: {dim}")
        return

    if out_png:
        print(f"{Path(filename).name} ({len(points)} pts, {dim}D) -> {out_png.name}")


def render_all(pattern: str = "mask_*.txt"):
    """Batch-renders every point dump in DATA_DIR matching `pattern` to a
    same-named .png alongside it, without opening any windows."""
    files = sorted(DATA_DIR.glob(pattern))
    if not files:
        print(f"No files matching {pattern!r} found in {DATA_DIR}")
        return

    for f in files:
        render_one(f, out_png=f.with_suffix(".png"), show=False)

    print(f"\nRendered {len(files)} file(s) from {DATA_DIR}")


def main():
    if "--all" in sys.argv:
        render_all()
        return

    render_one(FILENAME, show=True)


if __name__ == "__main__":
    main()
