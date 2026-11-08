#ifndef CONVEX_3D_WALK_H
#define CONVEX_3D_WALK_H

// The two 3D walk mechanics, mirroring the 2D engine (convex_2d/walk.h):
//   - HARD (doc section 1.5/1.6): neighbors outside the shape are excluded;
//     the walker never leaves.
//   - SOFT (doc section 4.2, extended to 3D): the walker may step outside.
//     Every one of the 6 candidate directions is weighted by
//     exp(-d(q)^distancePower / temperature), where d(q) is the 6-connected
//     lattice-step (BFS) distance from q back to the shape.
// Either way the run stops once 50% of interior lattice points have been
// visited; points visited while outside the shape don't count.
// The toroidal dragging-force walk (doc section 4.3) stays 2D-square only.

#include <random>
#include <vector>

#include "shapes.h"

enum class WalkMode3D
{
    HARD,
    SOFT
};

struct WalkParams3D
{
    WalkMode3D mode = WalkMode3D::HARD;

    // SOFT mode: w_i = exp(-d_i^distancePower / temperature).
    double temperature = 1.0;
    double distancePower = 2.0;
};

struct WalkContext3D
{
    std::mt19937_64 rng;
};

// Returns the number of steps taken. If non-null, outPath receives every
// position visited (only the one sampled run per index asks for it).
int walk(int index, ShapeType3D shape, const WalkParams3D &params, WalkContext3D &ctx,
         std::vector<LatticePoint3D> *outPath = nullptr);

#endif // CONVEX_3D_WALK_H
