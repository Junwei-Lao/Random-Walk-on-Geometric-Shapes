"""
Plots a shape's boundary together with a random walker's visited path, and
(optionally) the disk-covering analysis's covering circles.

This consolidates three previously separate scripts:
  - path_with_cover.py   (the walk + shape boundary + cover-circle plotting)
  - SlottedRect_30x10.py (boundary-corner generator for SLOTTED_RECT_30x10)
  - double_bowtie.py     (boundary-corner generator for DOUBLE_BOWTIE)
into shape generators feeding a single get_shape_polygon() dispatch, so this
is the one file in path_visual/.

Along the way, SlottedRect_30x10.py's boundary generator had a real bug:
its xs list had 9 points but its ys list had 13, which would raise from
plt.plot() -- it never surfaced because that script only ever printed the
lists, never plotted them. The corrected outline is below in
slotted_rect_30x10_boundary().

Note: the current C++ engine's disk-covering analysis (src/convex_2d/cover.cpp)
only tracks a coverage *fraction*, not individual disk-center coordinates, so
it no longer emits a "cover.txt"-style file for show_cover=True to read here.
Set show_cover=False (or point cover_file at your own disk-center dump) if
you don't have one.
"""

import math
import matplotlib.pyplot as plt
from matplotlib.patches import Circle


# =============================
# Configuration -- edit as needed
# =============================
show_cover = False
DEFAULT_RADIUS = 0.03
index = 100
shape_name = "polygon6"  # e.g. "circle", "polygon8", "slotted_rect_30x10", "double_bowtie", ...
walk_file = "walked.txt"
cover_file = "cover.txt"


# =============================
# File readers
# =============================
def read_walked_points(filename: str):
    xs, ys = [], []
    with open(filename, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            x, y = map(float, line.split())
            xs.append(x)
            ys.append(y)
    return xs, ys


def read_cover_centers(filename: str, default_radius: float = DEFAULT_RADIUS):
    xs, ys = [], []
    radius = default_radius

    with open(filename, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            if line.startswith("#") and "radius" in line:
                try:
                    radius = float(line.split("=")[1])
                except Exception:
                    pass
                continue

            if line.startswith("#"):
                continue

            x, y = map(float, line.split())
            xs.append(x)
            ys.append(y)

    return xs, ys, radius


# =============================
# Geometry helpers
# =============================
def ensure_closed_polygon(xs, ys):
    if not xs or not ys:
        return xs, ys
    if xs[0] != xs[-1] or ys[0] != ys[-1]:
        xs = list(xs) + [xs[0]]
        ys = list(ys) + [ys[0]]
    return xs, ys


# =============================
# Shape boundary generators
# =============================
def circle_boundary(index: float, center=None, num_pts: int = 720):
    """
    Returns (xs, ys) points along the circle boundary.
    Default: center=(index, index), radius=index (matches the C++ engine's
    "shifted circle" convention, src/convex_2d/shapes.cpp inBoundary CIRCLE).
    """
    r = float(index)
    cx, cy = (r, r) if center is None else center

    xs, ys = [], []
    for i in range(num_pts + 1):
        t = 2.0 * math.pi * i / num_pts
        xs.append(cx + r * math.cos(t))
        ys.append(cy + r * math.sin(t))
    return xs, ys


def regular_polygon(index: float, N: int, margin: float = 20.0, clockwise: bool = True):
    """
    General regular N-gon matching src/convex_2d/shapes.cpp's inRegularPolygon:
      - circumradius R = index * sqrt(2*pi / (N * sin(2*pi/N))), so area = pi * index^2
      - center shifted by (R + margin, R + margin)
    """
    if N < 3:
        raise ValueError("N must be >= 3 for a polygon.")

    two_pi_over_N = 2.0 * math.pi / N
    denom = N * math.sin(two_pi_over_N)
    if denom == 0.0:
        raise ValueError("Invalid N leading to zero denominator.")

    R = float(index) * math.sqrt((2.0 * math.pi) / denom)
    cx = R + float(margin)
    cy = R + float(margin)

    angles = [((2 * k + 1) * math.pi / N) for k in range(N)]
    if clockwise:
        angles = list(reversed(angles))

    xs = [cx + R * math.cos(t) for t in angles]
    ys = [cy + R * math.sin(t) for t in angles]
    return ensure_closed_polygon(xs, ys)


def slotted_rect_30x10_boundary(index: float):
    """
    Boundary of SLOTTED_RECT_30x10: a 60a x 10a outer rectangle with a slot
    of thickness 0.5a cut in from the right edge, starting 1a from the left
    (matches src/convex_2d/shapes.cpp's slottedRect30x10Dims exactly; despite
    the shape's name, its outer width constant is 60, not 30 -- inherited
    naming from the original enum/doc, not something introduced here).

    The slot reaches the right edge (slot_x1 == W), so the outline is a
    single closed, simply-connected "C" shape rather than a rectangle with
    an enclosed hole.
    """
    a = math.sqrt(math.pi / 570.5) * index

    W = 60 * a
    H = 10 * a
    slot_x0 = 1 * a
    slot_h = 0.5 * a
    slot_y0 = 5 * a - slot_h / 2
    slot_y1 = 5 * a + slot_h / 2

    xs = [0, W, W, slot_x0, slot_x0, W, W, 0, 0]
    ys = [0, 0, slot_y0, slot_y0, slot_y1, slot_y1, H, H, 0]
    return xs, ys


def double_bowtie_boundary(index: float):
    """
    Outer boundary of DOUBLE_BOWTIE: the union of a horizontal bowtie and a
    vertical bowtie sharing a 13a x 13a bounding box (matches
    src/convex_2d/shapes.cpp's DOUBLE_BOWTIE case exactly).
    """
    a = math.sqrt(math.pi / 89.0) * index

    x0, x2, x4, x6, x7, x9, x11, x13 = (k * a for k in (0, 2, 4, 6, 7, 9, 11, 13))
    y0, y2, y4, y6, y7, y9, y11, y13 = (k * a for k in (0, 2, 4, 6, 7, 9, 11, 13))

    pts = [
        (x2, y13), (x11, y13), (x7, y9), (x7, y7), (x9, y7), (x13, y11),
        (x13, y2), (x9, y6), (x7, y6), (x7, y4), (x11, y0), (x2, y0),
        (x6, y4), (x6, y6), (x4, y6), (x0, y2), (x0, y11), (x4, y7),
        (x6, y7), (x6, y9), (x2, y13),
    ]
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return xs, ys


def get_shape_polygon(shape_name: str, index: float):
    s = (shape_name or "").strip().lower()

    if s == "circle":
        return circle_boundary(index)

    if s in ("slotted_rect_30x10", "slottedrect", "slotted_rect"):
        return slotted_rect_30x10_boundary(index)

    if s in ("double_bowtie", "doublebowtie"):
        return double_bowtie_boundary(index)

    # Generic "polygonN" / "polyN" / "pN" support, e.g. "polygon5", "p8".
    for prefix in ("polygon", "poly", "p"):
        if s.startswith(prefix):
            tail = s[len(prefix):].strip()
            if tail.isdigit():
                return regular_polygon(index, int(tail))

    if s == "hexagon":
        return regular_polygon(index, 6)

    raise ValueError(f"Unknown shape_name='{shape_name}'.")


# =============================
# Main plotting routine
# =============================
def main():
    shape_xs, shape_ys = get_shape_polygon(shape_name, index)
    walk_x, walk_y = read_walked_points(walk_file)

    cover_x, cover_y, radius = [], [], DEFAULT_RADIUS
    if show_cover:
        cover_x, cover_y, radius = read_cover_centers(cover_file)

    fig, ax = plt.subplots(figsize=(8, 8))

    ax.plot(shape_xs, shape_ys, color="black", linewidth=2.0, label="Shape boundary", zorder=1)
    ax.scatter(walk_x, walk_y, c="red", s=10, label="Walked points", zorder=3)

    if show_cover and cover_x:
        for cx, cy in zip(cover_x, cover_y):
            ax.add_patch(Circle((cx, cy), radius, edgecolor="blue", facecolor="none",
                                 linewidth=1.2, alpha=0.6, zorder=2))
        ax.scatter(cover_x, cover_y, c="blue", s=20, label="Disk centers", zorder=4)

    all_x = list(shape_xs) + list(walk_x) + list(cover_x)
    all_y = list(shape_ys) + list(walk_y) + list(cover_y)
    if all_x and all_y:
        xmin, xmax = min(all_x), max(all_x)
        ymin, ymax = min(all_y), max(all_y)
        pad = radius if (show_cover and cover_x) else 0.0
        extra = 0.05 * max(1.0, (xmax - xmin), (ymax - ymin))
        ax.set_xlim(xmin - pad - extra, xmax + pad + extra)
        ax.set_ylim(ymin - pad - extra, ymax + pad + extra)

    ax.set_aspect("equal", adjustable="box")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title(f"Walk on shape '{shape_name}' (index={index})")
    ax.grid(True, linestyle="--", alpha=0.4)
    ax.legend()

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
