"""
Plots a shape's boundary together with a random walker's visited path, and
(optionally) the disk-covering analysis's covering circles.

Every shape in src/convex_2d/shapes.cpp's ShapeType2D enum has a boundary
generator here (all 21: CIRCLE, RECTANGLE_3_1, RECTANGLE_4_1, SQUARE,
TRIANGLE, SHARP_TRIANGLE, FLAT_TRIANGLE, HEXAGON, POLYGON5/8/9/10/12/15,
ROTATED_SQUARE, STRETCHED_ROTATED_SQUARE, BOWTIE, DOUBLE_BOWTIE,
SLOTTED_RECT_30x10, V_NOTCH_RECT, SNOWFLAKE), each ported directly from that
file's inBoundary formulas so the drawn outline matches the C++ engine
exactly. shape_name accepts the same string the C++ --shape flag does
(case-insensitively), e.g. "SQUARE" or "square".

get_shape_outlines() returns a *list* of (xs, ys) closed polylines rather
than a single one: every shape but SNOWFLAKE returns a one-element list,
but SNOWFLAKE has no single boundary polygon at all -- it's a union of many
axis-aligned rectangles from a recursive fractal -- so it returns one
outline per rectangle. main() below just draws whatever list it gets.

This file originally consolidated three separate scripts (path_with_cover.py,
SlottedRect_30x10.py, double_bowtie.py); SlottedRect_30x10.py's boundary
generator had a real bug fixed during that merge: its xs list had 9 points
but its ys list had 13, which would raise from plt.plot() -- it never
surfaced because that script only ever printed the lists, never plotted them.

Note: the current C++ engine's disk-covering analysis (src/convex_2d/cover.cpp)
only tracks a coverage *fraction*, not individual disk-center coordinates, so
it no longer emits a "cover.txt"-style file for show_cover=True to read here.
Set show_cover=False (or point cover_file at your own disk-center dump) if
you don't have one.

All C++ engines save their output (CSVs and sample walker paths alike) into
data/ at the repo root (see the makefile's OUTDIR / each binary's --outdir
default). DATA_DIR below is computed relative to this script's own location,
so it resolves correctly no matter what directory you run the script from.
"""

import math
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import Circle

# repo_root/visualization/path_visual/path_visualizer.py -> repo_root/data
DATA_DIR = Path(__file__).resolve().parent.parent.parent / "data"


# =============================
# Configuration -- edit as needed
# =============================
show_cover = False
DEFAULT_RADIUS = 0.03
index = 50
shape_name = "polygon8"  # any ShapeType2D name, e.g. "hexagon", "snowflake", "double_bowtie", ...

# Matches the C++ naming convention path_{SHAPE}_{MODE}_{INDEX}.txt, e.g. the
# file produced by `make run-2d SHAPE=SQUARE MODE=hard INDEX_MIN=50 INDEX_MAX=50`.
walk_file = DATA_DIR / "path_POLYGON8_hard_50.txt"
cover_file = DATA_DIR / "cover.txt"


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
# Shape boundary generators -- each one ported directly from the matching
# inBoundary case in src/convex_2d/shapes.cpp.
# =============================
def circle_boundary(index: float, center=None, num_pts: int = 720):
    """
    CIRCLE: center=(index, index), radius=index (matches the C++ engine's
    "shifted circle" convention).
    """
    r = float(index)
    cx, cy = (r, r) if center is None else center

    xs, ys = [], []
    for i in range(num_pts + 1):
        t = 2.0 * math.pi * i / num_pts
        xs.append(cx + r * math.cos(t))
        ys.append(cy + r * math.sin(t))
    return xs, ys


def square_boundary(index: float):
    """SQUARE: side s = sqrt(pi) * index, axis-aligned at the origin."""
    s = math.sqrt(math.pi) * index
    return ensure_closed_polygon([0, s, s, 0], [0, 0, s, s])


def rectangle_boundary(index: float, ratio: int):
    """RECTANGLE_3_1 / RECTANGLE_4_1: width:height = ratio:1, area = pi*index^2."""
    c = math.sqrt(math.pi / ratio)
    w = ratio * c * index
    h = c * index
    return ensure_closed_polygon([0, w, w, 0], [0, 0, h, h])


def rectangle_3_1_boundary(index: float):
    return rectangle_boundary(index, 3)


def rectangle_4_1_boundary(index: float):
    return rectangle_boundary(index, 4)


def isosceles_triangle_boundary(index: float, base_angle_deg: float):
    """
    TRIANGLE (base_angle=60, i.e. equilateral) / SHARP_TRIANGLE (75) /
    FLAT_TRIANGLE (40): base along y=0 from x=0 to x=c*index, apex at
    x=c*index/2, where c = sqrt(4*pi / tan(base_angle)).
    """
    base_angle = math.radians(base_angle_deg)
    c = math.sqrt(4.0 * math.pi / math.tan(base_angle)) * index
    apex_y = math.tan(base_angle) * c / 2.0
    return ensure_closed_polygon([0, c, c / 2.0], [0, 0, apex_y])


def triangle_boundary(index: float):
    return isosceles_triangle_boundary(index, 60.0)


def sharp_triangle_boundary(index: float):
    return isosceles_triangle_boundary(index, 75.0)


def flat_triangle_boundary(index: float):
    return isosceles_triangle_boundary(index, 40.0)


def regular_polygon(index: float, N: int, margin: float = 20.0, clockwise: bool = True):
    """
    HEXAGON / POLYGON5/8/9/10/12/15: matches inRegularPolygon:
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

    # inRegularPolygon folds each candidate point's angle into a canonical
    # sector via theta_folded = ((theta + pi) mod sector) - sector/2, and
    # accepts it iff r*cos(theta_folded) <= apothem. A vertex sits exactly
    # where that boundary is tightest, i.e. theta_folded = +-sector/2, which
    # solves to theta_vertex = -pi (mod sector). For odd N this reduces to
    # (2k+1)*sector/2 (vertex opposite a flat side), but for *even* N it
    # reduces to k*sector instead (vertex sits at angle 0, not sector/2) --
    # a distinct case this used to get wrong, rotating hexagons/octagons/
    # decagons by half a sector relative to the true C++ shape.
    phase = (-math.pi) % two_pi_over_N
    angles = [phase + k * two_pi_over_N for k in range(N)]
    if clockwise:
        angles = list(reversed(angles))

    xs = [cx + R * math.cos(t) for t in angles]
    ys = [cy + R * math.sin(t) for t in angles]
    return ensure_closed_polygon(xs, ys)


def rotated_square_boundary(index: float):
    """
    ROTATED_SQUARE: side s = sqrt(pi)*index square rotated 45 deg. The C++
    inBoundary test maps a *candidate point* (x, y) into un-rotated test
    space via rx = x*cos(-45) - (y-s)*sin(-45), ry = x*sin(-45) +
    (y-s)*cos(-45) + s, then checks rx, ry in [0, s]. To draw the boundary
    in original (x, y) space we apply the algebraic inverse of that map to
    the test-space square's 4 corners.
    """
    s = math.sqrt(math.pi) * index
    cos45 = math.cos(math.radians(45))
    sin45 = math.sin(math.radians(45))

    def inverse_map(rx, ry):
        x = rx * cos45 - (ry - s) * sin45
        y = rx * sin45 + (ry - s) * cos45 + s
        return x, y

    corners = [(0, 0), (s, 0), (s, s), (0, s)]
    mapped = [inverse_map(rx, ry) for rx, ry in corners]
    return ensure_closed_polygon([p[0] for p in mapped], [p[1] for p in mapped])


def stretched_rotated_square_boundary(index: float):
    """
    STRETCHED_ROTATED_SQUARE: a diamond with horizontal:vertical extent
    ratio 3:1, implemented in C++ as two triangles. At x=0 and x=xlength the
    vertical extent collapses to a point (the diamond's left/right tips);
    at x=xlength/2 it's the full height (the top/bottom tips).
    """
    ylength = math.sqrt(2.0 * math.pi / 3.0) * index
    xlength = 3.0 * ylength
    xs = [0, xlength / 2.0, xlength, xlength / 2.0]
    ys = [ylength / 2.0, 0, ylength / 2.0, ylength]
    return ensure_closed_polygon(xs, ys)


def bowtie_boundary(index: float):
    """
    BOWTIE: two wide triangular ends joined by a thin rectangular waist of
    thickness `width`. Matches the inBoundary case's three x-ranges exactly.
    """
    width = math.sqrt(math.pi / 45.0) * index
    length = 5.0 * width
    hyposide = 4.0 * width
    height = width + 2.0 * hyposide
    total_len = length + 2.0 * hyposide

    xs = [0, hyposide, hyposide + length, total_len, total_len, hyposide + length, hyposide, 0]
    ys = [0, hyposide, hyposide, 0, height, hyposide + width, hyposide + width, height]
    return ensure_closed_polygon(xs, ys)


def double_bowtie_boundary(index: float):
    """
    DOUBLE_BOWTIE: the union of a horizontal bowtie and a vertical bowtie
    sharing a 13a x 13a bounding box.
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


def slotted_rect_30x10_boundary(index: float):
    """
    SLOTTED_RECT_30x10: a 60a x 10a outer rectangle with a slot of thickness
    0.5a cut in from the right edge, starting 1a from the left (despite the
    shape's name, its outer width constant is 60, not 30 -- inherited naming
    from the original enum/doc). The slot reaches the right edge, so the
    outline is a single closed, simply-connected "C" shape.
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


def v_notch_rect_boundary(index: float):
    """
    V_NOTCH_RECT: a 4a x 6a rectangle whose entire top edge is replaced by a
    V notch reaching down to apex (2a, a) -- the V's two ends land exactly
    on the rectangle's top corners.
    """
    a = index * math.sqrt(math.pi / 14.0)
    W = 4.0 * a
    H = 6.0 * a
    xs = [0, W, W, 2.0 * a, 0]
    ys = [0, 0, H, a, H]
    return ensure_closed_polygon(xs, ys)


# ---- SNOWFLAKE: a union of rectangles, not a single polygon ----
def _emit_rect(rects, x0, x1, y0, y1):
    rects.append((min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1)))


def _emit_cross_full(rects, cx, cy, arm, thk):
    _emit_rect(rects, cx - arm, cx + arm, cy - thk / 2.0, cy + thk / 2.0)
    _emit_rect(rects, cx - thk / 2.0, cx + thk / 2.0, cy - arm, cy + arm)


def _emit_cross_outward(rects, cx, cy, arm, thk, parentx, parenty, eps=1e-9):
    d0 = (cx - parentx) ** 2 + (cy - parenty) ** 2

    def outward(ex, ey):
        d1 = (ex - parentx) ** 2 + (ey - parenty) ** 2
        return d1 > d0 + eps

    if outward(cx + arm, cy):
        _emit_rect(rects, cx, cx + arm, cy - thk / 2.0, cy + thk / 2.0)
    if outward(cx - arm, cy):
        _emit_rect(rects, cx - arm, cx, cy - thk / 2.0, cy + thk / 2.0)
    if outward(cx, cy + arm):
        _emit_rect(rects, cx - thk / 2.0, cx + thk / 2.0, cy, cy + arm)
    if outward(cx, cy - arm):
        _emit_rect(rects, cx - thk / 2.0, cx + thk / 2.0, cy - arm, cy)


def _emit_cross_fractal_outward(rects, cx, cy, arm, thk, parentx, parenty, depth, scale_len, scale_thk, eps=1e-9):
    if arm <= 0.0 or thk <= 0.0 or depth <= 0:
        return

    _emit_cross_outward(rects, cx, cy, arm, thk, parentx, parenty, eps=eps)
    if depth <= 1:
        return

    d0 = (cx - parentx) ** 2 + (cy - parenty) ** 2

    def try_recurse(nx, ny):
        d1 = (nx - parentx) ** 2 + (ny - parenty) ** 2
        if d1 > d0 + eps:
            _emit_cross_fractal_outward(rects, nx, ny, arm * scale_len, thk * scale_thk,
                                         cx, cy, depth - 1, scale_len, scale_thk, eps=eps)

    try_recurse(cx + arm, cy)
    try_recurse(cx - arm, cy)
    try_recurse(cx, cy + arm)
    try_recurse(cx, cy - arm)


def snowflake_rectangles(index: float):
    """Returns the list of (x0, x1, y0, y1) rectangles whose union is
    SNOWFLAKE, matching src/convex_2d/shapes.cpp's SNOWFLAKE case exactly:
    two hub crosses joined by a connector bar, each hub's three outer
    endpoints growing a recursive "+"-fractal that only branches away from
    its parent cross center."""
    a = math.sqrt(math.pi / 140.0) * index

    hub_thk = 1.0 * a
    hub_arm = 2.0 * a
    connector_len = 1.6 * a

    depth = 3
    scale_len = 0.55
    scale_thk = 0.50

    end_arm0 = 1.9 * a
    end_thk0 = 0.55 * a

    cy = 40.0 * a
    cxL = 40.0 * a - (connector_len / 2.0 + hub_arm)
    cxR = 40.0 * a + (connector_len / 2.0 + hub_arm)

    rects = []
    _emit_rect(rects, cxL + hub_arm, cxR - hub_arm, cy - hub_thk / 2.0, cy + hub_thk / 2.0)
    _emit_cross_full(rects, cxL, cy, hub_arm, hub_thk)
    _emit_cross_full(rects, cxR, cy, hub_arm, hub_thk)

    _emit_cross_fractal_outward(rects, cxL - hub_arm, cy, end_arm0, end_thk0, cxL, cy, depth, scale_len, scale_thk)
    _emit_cross_fractal_outward(rects, cxL, cy + hub_arm, end_arm0, end_thk0, cxL, cy, depth, scale_len, scale_thk)
    _emit_cross_fractal_outward(rects, cxL, cy - hub_arm, end_arm0, end_thk0, cxL, cy, depth, scale_len, scale_thk)
    _emit_cross_fractal_outward(rects, cxR + hub_arm, cy, end_arm0, end_thk0, cxR, cy, depth, scale_len, scale_thk)
    _emit_cross_fractal_outward(rects, cxR, cy + hub_arm, end_arm0, end_thk0, cxR, cy, depth, scale_len, scale_thk)
    _emit_cross_fractal_outward(rects, cxR, cy - hub_arm, end_arm0, end_thk0, cxR, cy, depth, scale_len, scale_thk)
    return rects


def snowflake_outlines(index: float):
    """One closed 4-corner outline per rectangle in the fractal decomposition."""
    outlines = []
    for x0, x1, y0, y1 in snowflake_rectangles(index):
        outlines.append(([x0, x1, x1, x0, x0], [y0, y0, y1, y1, y0]))
    return outlines


def get_shape_outlines(shape_name: str, index: float):
    """
    Dispatches on the same shape name the C++ engine's --shape flag takes
    (case-insensitively). Returns a list of (xs, ys) closed polylines --
    one element for every shape except SNOWFLAKE, which returns one per
    rectangle in its fractal decomposition.
    """
    s = (shape_name or "").strip().lower()

    if s == "circle":
        return [circle_boundary(index)]
    if s == "square":
        return [square_boundary(index)]
    if s in ("rectangle_3_1", "rectangle3x1", "rect_3_1"):
        return [rectangle_3_1_boundary(index)]
    if s in ("rectangle_4_1", "rectangle4x1", "rect_4_1"):
        return [rectangle_4_1_boundary(index)]
    if s == "triangle":
        return [triangle_boundary(index)]
    if s == "sharp_triangle":
        return [sharp_triangle_boundary(index)]
    if s == "flat_triangle":
        return [flat_triangle_boundary(index)]
    if s == "hexagon":
        return [regular_polygon(index, 6)]
    if s == "rotated_square":
        return [rotated_square_boundary(index)]
    if s == "stretched_rotated_square":
        return [stretched_rotated_square_boundary(index)]
    if s == "bowtie":
        return [bowtie_boundary(index)]
    if s in ("double_bowtie", "doublebowtie"):
        return [double_bowtie_boundary(index)]
    if s in ("slotted_rect_30x10", "slottedrect", "slotted_rect"):
        return [slotted_rect_30x10_boundary(index)]
    if s == "v_notch_rect":
        return [v_notch_rect_boundary(index)]
    if s == "snowflake":
        return snowflake_outlines(index)

    # Generic "polygonN" / "polyN" / "pN" support, e.g. "polygon5", "p8".
    for prefix in ("polygon", "poly", "p"):
        if s.startswith(prefix):
            tail = s[len(prefix):].strip()
            if tail.isdigit():
                return [regular_polygon(index, int(tail))]

    raise ValueError(f"Unknown shape_name='{shape_name}'.")


# =============================
# Main plotting routine
# =============================
def main():
    shape_outlines = get_shape_outlines(shape_name, index)
    walk_x, walk_y = read_walked_points(walk_file)

    cover_x, cover_y, radius = [], [], DEFAULT_RADIUS
    if show_cover:
        cover_x, cover_y, radius = read_cover_centers(cover_file)

    fig, ax = plt.subplots(figsize=(8, 8))

    many_pieces = len(shape_outlines) > 1
    for i, (oxs, oys) in enumerate(shape_outlines):
        ax.plot(oxs, oys, color="black", linewidth=1.0 if many_pieces else 2.0,
                label="Shape boundary" if i == 0 else None, zorder=1)

    ax.scatter(walk_x, walk_y, c="red", s=10, label="Walked points", zorder=3)

    if show_cover and cover_x:
        for cx, cy in zip(cover_x, cover_y):
            ax.add_patch(Circle((cx, cy), radius, edgecolor="blue", facecolor="none",
                                 linewidth=1.2, alpha=0.6, zorder=2))
        ax.scatter(cover_x, cover_y, c="blue", s=20, label="Disk centers", zorder=4)

    shape_xs = [x for oxs, _ in shape_outlines for x in oxs]
    shape_ys = [y for _, oys in shape_outlines for y in oys]
    all_x = shape_xs + list(walk_x) + list(cover_x)
    all_y = shape_ys + list(walk_y) + list(cover_y)
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
