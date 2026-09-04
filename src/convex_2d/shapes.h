#ifndef CONVEX_2D_SHAPES_H
#define CONVEX_2D_SHAPES_H

// All 2D shapes investigated by the project (doc section 1.4), unified under
// a single enum. Earlier revisions of this project split "convex" and
// "non-convex" shapes into separate directories/enums with duplicated logic;
// they are merged here since every 2D shape shares the same lattice, the
// same inBoundary/getPointsInShape/initialize contract, and the same walk
// engine. `TRAPEZOID` (present in an earlier revision) is not part of the
// documented shape list and has been dropped, matching the project's other
// undocumented-shape cleanup (see 3D CYLINDER removal).

#include <random>
#include <string>
#include <utility>
#include <vector>

#include "hash.h"

extern const double PI;

enum class ShapeType2D
{
    CIRCLE,
    RECTANGLE_3_1,
    RECTANGLE_4_1,
    SQUARE,
    TRIANGLE, // equilateral
    SHARP_TRIANGLE,
    FLAT_TRIANGLE,
    HEXAGON,
    POLYGON5,
    POLYGON8,
    POLYGON9,
    POLYGON10,
    POLYGON12,
    POLYGON15,
    ROTATED_SQUARE,
    STRETCHED_ROTATED_SQUARE,
    BOWTIE,
    DOUBLE_BOWTIE,
    SLOTTED_RECT_30x10,
    V_NOTCH_RECT,
    SNOWFLAKE
};

// All shapes paired with their canonical CLI/file-name string, in the order
// they appear in the project documentation. Used for --list-shapes and for
// parsing --shape=NAME.
extern const std::vector<std::pair<ShapeType2D, std::string>> kAllShapes2D;

std::string shapeName(ShapeType2D shape);
bool shapeFromName(const std::string &name, ShapeType2D &out);

// True iff lattice point (x, y) lies inside or on the boundary of `shape`
// scaled to size index `index` (area == PI * index^2, per doc section 1.3).
bool inBoundary(int x, int y, int index, ShapeType2D shape);

// Counts interior lattice points. If `mask` is non-null, also allocates and
// fills a `(*xLen) x (*yLen)` boolean grid (caller must free with
// freeMask2D). All shapes are laid out with x, y >= 0.
int getPointsInShape(int index, ShapeType2D shape, bool ***mask = nullptr, int *xLen = nullptr, int *yLen = nullptr);
void freeMask2D(bool **mask, int xLen);

// Samples a uniformly random interior lattice point via rejection sampling.
void initialize(LatticePoint2D &start, ShapeType2D shape, int index, std::mt19937_64 &rng);

struct ConnectedShape2D
{
    bool **mask = nullptr;
    int xLen = 0;
    int yLen = 0;
    int pointCount = 0; // size of the largest 4-connected component
};

// Restricts the raw inBoundary rasterization to its largest 4-connected
// component. This guards against fractal or thinly-pinched shapes (e.g.
// SNOWFLAKE) whose rasterization can leave a handful of lattice points
// isolated from the main body: a hard-boundary walk seeded on such an
// island can never reach a 50%/75% mixing threshold defined over the whole
// shape, since 4-connected moves can never leave the island (this would
// violate Remark 1's "finite and connected" assumption and loop forever).
// For every shape without such artifacts (the overwhelming majority), this
// simply returns the same mask getPointsInShape would have built.
ConnectedShape2D buildConnectedShape(int index, ShapeType2D shape);
void freeConnectedShape(ConnectedShape2D &cs);

// Uniformly samples one of the `pointCount` lattice points kept in `cs.mask`.
LatticePoint2D sampleUniform(const ConnectedShape2D &cs, std::mt19937_64 &rng);

// Lattice-step (4-connected) distance from `from` to the nearest interior
// point of the shape; 0 if `from` is already interior. Used by the
// soft-boundary walk (doc section 1.8) to compute the return distance d(q).
int distanceToShape(LatticePoint2D from, int index, ShapeType2D shape);

// Number of distinct lattice x-values (equivalently y-values, since SQUARE is
// a perfect box) inside SQUARE at this index. Used as the wrap-around
// modulus for the toroidal dragging-force walk (doc section 1.9), which is
// defined exclusively for the square.
int squareLatticeModulus(int index);

#endif // CONVEX_2D_SHAPES_H
