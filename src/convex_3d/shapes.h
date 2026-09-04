#ifndef CONVEX_3D_SHAPES_H
#define CONVEX_3D_SHAPES_H

// The 5 3D convex shapes investigated by the project (doc section 1.4).
//
// A previous revision declared PYRAMID in the enum but never implemented it
// (inBoundary/getPointsInShape/initialize all lacked a case, so selecting it
// would throw at runtime), and included an undocumented CYLINDER shape. This
// version implements PYRAMID and drops CYLINDER, matching the documented
// shape list exactly.

#include <random>
#include <string>
#include <utility>
#include <vector>

#include "hash.h"

extern const double PI;

enum class ShapeType3D
{
    SPHERE,
    OCTAHEDRON,
    CUBE,
    RECTANGULAR_PRISM_1_2_3,
    PYRAMID
};

extern const std::vector<std::pair<ShapeType3D, std::string>> kAllShapes3D;

std::string shapeName(ShapeType3D shape);
bool shapeFromName(const std::string &name, ShapeType3D &out);

// True iff lattice point (x, y, z) lies inside or on the boundary of `shape`
// scaled to size index `index` (volume == (4/3) * PI * index^3, doc 1.3).
bool inBoundary(int x, int y, int z, int index, ShapeType3D shape);

int getPointsInShape(int index, ShapeType3D shape);

// An axis-aligned integer box guaranteed to contain the entire shape at this
// index (with a small margin), as [xMin, xMax) x [yMin, yMax) x [zMin, zMax).
// Exposed for tools that need to rasterize the shape directly (see
// tools/generate_mask_3d.cpp).
void boundingBox(int index, ShapeType3D shape, int &xMin, int &xMax, int &yMin, int &yMax, int &zMin, int &zMax);

void initialize(LatticePoint3D &start, ShapeType3D shape, int index, std::mt19937_64 &rng);

#endif // CONVEX_3D_SHAPES_H
