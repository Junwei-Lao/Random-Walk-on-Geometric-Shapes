#ifndef CONVEX_3D_WALK_H
#define CONVEX_3D_WALK_H

// The 3D walk is hard-boundary only: the soft-boundary and toroidal
// dragging-force extensions (doc sections 1.8/1.9) are explicitly scoped to
// 2D shapes in the documentation, so there is no 3D equivalent to build.
// Stops once 75% of interior lattice points have been visited (doc 1.6).

#include <random>
#include <vector>

#include "shapes.h"

struct WalkContext3D
{
    std::mt19937_64 rng;
};

int walk(int index, ShapeType3D shape, WalkContext3D &ctx, std::vector<LatticePoint3D> *outPath = nullptr);

#endif // CONVEX_3D_WALK_H
