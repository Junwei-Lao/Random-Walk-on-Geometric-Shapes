# Random Walk on Geometric Shapes

Simulation code for a study of random-walk mixing times inside 2D and 3D
geometric shapes. Full methodology (shape definitions, walk mechanics,
stopping criteria, the soft-boundary/toroidal-drag extensions, and the
disk-covering analysis) is in the project documentation PDF; this repo is
the implementation.

## Requirements

- `g++` with C++17 support
- `make`
- [CGAL](https://www.cgal.org/) + GMP + MPFR, for the two convex-hull engines
  (Ubuntu/Debian: `sudo apt install libcgal-dev`)

## Layout

Four independent simulation engines, plus two small visualization tools,
each built as its own binary and driven entirely by command-line flags --
nothing is hard-coded in the `.cpp` files. The Makefile is the single place
that lists what to run.

```
src/
  common/cli_args.h          Shared --flag parser + mean/stddev helper
  convex_2d/                 2D analytic engine: 21 shapes (16 convex + 5
                              non-convex, doc section 1.4), closed-form
                              inBoundary formulas. Hard/soft/drag walk modes
                              (doc 1.5/1.8/1.9) and disk-covering (doc 1.10).
  convex_3d/                 3D analytic engine: 5 convex shapes, hard-
                              boundary walk only (the soft/drag extensions
                              are 2D-only per the doc).
  convex_2d_convexhull/      2D convex-hull engine (CGAL): the 15 polygonal
                              2D convex shapes, defined by vertex sets and
                              rasterized via CGAL::convex_hull_2 instead of
                              closed-form formulas.
  convex_3d_convexhull/      3D convex-hull engine (CGAL): OCTAHEDRON, CUBE,
                              RECTANGULAR_PRISM_1_2_3, PYRAMID, defined by
                              vertex sets and rasterized via CGAL::convex_hull_3.
tools/
  generate_mask_2d.cpp       Dumps a shape's interior (or shell-only) lattice
  generate_mask_3d.cpp       points to a text file, for visualization.
data/                        Default output for all four engines and both
                              mask tools: summary/distribution CSVs, sample
                              walker paths (--record-path), and mask/point
                              dumps all land here (override with --outdir).
visualization/
  path_visual/path_visualizer.py   Plots a shape boundary + a walked path
                                    (+ optional cover circles) from data/.
  shell_visual/shell_visualizer.py Scatter-plots a 2D/3D point dump from data/.
```

`CIRCLE` (2D) and `SPHERE` (3D) have no finite vertex set, so they exist
only in the analytic engines, not the convex-hull ones -- mirroring how the
convex-hull engines only ever covered the polygonal/polyhedral shapes.

## Build

```
make build      # builds bin/walk2d, walk3d, hull2d, hull3d, mask2d, mask3d
make clean      # removes obj/ and bin/
make help       # full variable/target reference
```

## Running

Every run parameter is a Makefile variable that gets forwarded as a CLI
flag -- override any of them on the command line:

```
make run-2d SHAPE=HEXAGON MODE=soft TEMPERATURE=1.5 INDEX_MIN=20 INDEX_MAX=100
make run-2d SHAPE=SQUARE MODE=drag DRAG_FORCE=500
make run-2d SHAPE=SNOWFLAKE COVER=1 RHO=0.02
make run-3d SHAPE3D=PYRAMID INDEX_MIN=10 INDEX_MAX=60
make run-2d-hull HULLSHAPE2D=HEXAGON
make run-3d-hull HULLSHAPE3D=OCTAHEDRON

# Sweep multiple shapes in one command: use the plural SHAPES variable
# (space-separated, not comma-separated) instead of SHAPE. Each run-* target
# loops the binary once per shape. SHAPE(S) is single-valued only -- a
# comma list like SHAPE="SQUARE,CIRCLE" would just fail as an unknown shape.
make run-2d SHAPES="SQUARE CIRCLE HEXAGON"
make run-2d SHAPES=ALL              # every 2D shape
make run-3d SHAPES3D=ALL            # every 3D shape
make run-2d-hull HULLSHAPES2D=ALL
make run-3d-hull HULLSHAPES3D=ALL

make list-shapes-2d         # print valid --shape values for each engine
make list-shapes-3d
make list-shapes-2d-hull
make list-shapes-3d-hull

make mask-2d SHAPE=SNOWFLAKE INDEX=50            # full interior dump
make mask-2d SHAPE=SNOWFLAKE INDEX=50 SHELL_ONLY=1  # boundary-only dump
```

`MODE=drag` is defined only for `SHAPE=SQUARE` (doc section 1.9) and the
binary will refuse to run it against any other shape. Binaries can also be
invoked directly, e.g. `./bin/walk2d --help` for the full flag list.

## Notes on this revision

This reorganizes an earlier version of the codebase and fixes several bugs
found along the way (each documented at its fix site in the source):

- The 2D hard-boundary and soft-boundary walks were dead code behind a
  compile-time macro; only an always-on toroidal+hardcoded-drag walk ran,
  for every shape, not just the square. All three modes are now real and
  independently selectable (`src/convex_2d/walk.cpp`).
- The disk-covering analysis placed disks at a random offset from the
  visited point rather than at the point itself, and didn't restrict
  covered points to the shape's interior. Fixed to match the documented
  definition exactly (`src/convex_2d/cover.cpp`).
- `SNOWFLAKE`'s thin fractal geometry can rasterize to a handful of lattice
  points disconnected from the main body; a hard-boundary walk seeded there
  would loop forever. The walk now seeds from (and measures against) only
  the largest 4-connected component (`src/convex_2d/shapes.cpp`).
- 3D `PYRAMID` was declared but unimplemented (would throw at runtime); it's
  now implemented with height equal to base side, matching the convex-hull
  engine's implicit choice. The undocumented `CYLINDER` shape was dropped.
- The 3D lattice-point hashmap used a lossy, collision-prone hash
  (`hx<<32 | hy<<16 | hz`, which drops bits of `y` and never shifts `z` at
  all); replaced with the same `unordered_set`-based approach as the 2D engine.
- The convex-hull engines' random walk used to choose uniformly among all
  directions and "stay in place" (still counting a step) whenever the
  chosen direction was invalid. This doesn't match the documented rule
  (uniform among *valid* neighbors) and inflated mixing times near the
  boundary; both hull engines now match the analytic engines' walk rule.
- `src/nonconvex_2d/` (an older BOWTIE/DOUBLEBOWTIE-only prototype) was
  removed; `convex_2d` already implements those two shapes plus three more
  under one unified `ShapeType2D` enum.
- `ROTATED_SQUARE`'s bounding box assumed the shape spans `[0, s*sqrt(2)]`
  in both x and y, but its rotation is centered at `(0, s)`, so the true
  y-range is `[s*(1-cos45), s*(1+cos45)]`. Since the rasterization loop
  always starts at y=0, this silently truncated the top ~8% of the shape's
  area; fixed by extending the box to the true upper bound
  (`src/convex_2d/shapes.cpp`).
- `path_visualizer.py` originally only drew boundaries for 5 of the 21 2D
  shapes (circle, the two named non-convex shapes, and generic regular
  polygons); it now has a generator for every `ShapeType2D` value, each
  ported from and numerically cross-checked against the real C++
  rasterization. That check also caught a second bug: the regular-polygon
  generator placed vertices at `(2k+1)*sector/2` for every N, which is only
  correct for odd N -- even N (hexagon, octagon, decagon, 12-gon) needed
  vertices at `k*sector` instead, a half-sector rotation off from what was
  there before.
- All four engines and both mask tools now save everything under one
  `--outdir` (default `data/`) instead of splitting CSVs, paths, and point
  dumps across `data/` and two `visualization/` subfolders. The
  `visualization/` scripts read from `data/` accordingly, resolved relative
  to each script's own file location so it works from any working directory.
