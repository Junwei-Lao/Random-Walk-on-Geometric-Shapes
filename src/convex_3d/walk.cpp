#include "walk.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
const std::array<LatticePoint3D, 6> kDirs{{{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}};

LatticePoint3D getNextPositionHard(LatticePoint3D pos, int index, ShapeType3D shape, std::mt19937_64 &rng)
{
    std::vector<int> allowed;
    for (int d = 0; d < 6; ++d)
    {
        LatticePoint3D next(pos.x + kDirs[d].x, pos.y + kDirs[d].y, pos.z + kDirs[d].z);
        if (inBoundary(next.x, next.y, next.z, index, shape))
            allowed.push_back(d);
    }

    if (allowed.empty())
    {
        std::cerr << "Warning: no valid moves from (" << pos.x << ", " << pos.y << ", " << pos.z
                   << "). Staying in place.\n";
        return pos;
    }

    std::uniform_int_distribution<std::size_t> pick(0, allowed.size() - 1);
    int d = allowed[pick(rng)];
    return LatticePoint3D(pos.x + kDirs[d].x, pos.y + kDirs[d].y, pos.z + kDirs[d].z);
}

// 3D version of the 2D soft-boundary step (doc eq. 4/5): w_i = exp(-d_i^p / T)
// over all 6 axis directions. Deep in the interior every d_i == 0, so the
// walk reduces to a uniform choice there, exactly as in 2D.
LatticePoint3D getNextPositionSoft(LatticePoint3D pos, int index, ShapeType3D shape,
                                    const WalkParams3D &params, std::mt19937_64 &rng)
{
    std::array<double, 6> weights{};
    for (int d = 0; d < 6; ++d)
    {
        LatticePoint3D next(pos.x + kDirs[d].x, pos.y + kDirs[d].y, pos.z + kDirs[d].z);
        int dist = distanceToShape(next, index, shape);
        weights[d] = std::exp(-std::pow(static_cast<double>(dist), params.distancePower) / params.temperature);
    }

    std::discrete_distribution<int> dist(weights.begin(), weights.end());
    int d = dist(rng);
    return LatticePoint3D(pos.x + kDirs[d].x, pos.y + kDirs[d].y, pos.z + kDirs[d].z);
}
} // namespace

int walk(int index, ShapeType3D shape, const WalkParams3D &params, WalkContext3D &ctx,
         std::vector<LatticePoint3D> *outPath)
{
    const int shapePoints = getPointsInShape(index, shape);
    const int target = shapePoints / 2; // 3D also stops at 50%

    LatticePoint3D pos;
    initialize(pos, shape, index, ctx.rng);

    PointSet3D visited;
    visited.reserve(static_cast<std::size_t>(shapePoints) / 2 + 1);

    std::vector<LatticePoint3D> path;
    if (outPath)
    {
        path.reserve(static_cast<std::size_t>(std::max(target + 5, 1000)));
        path.push_back(pos);
    }

    if (inBoundary(pos.x, pos.y, pos.z, index, shape))
        visited.insert(pos);

    int steps = 0;
    while (static_cast<int>(visited.size()) < target)
    {
        switch (params.mode)
        {
        case WalkMode3D::HARD:
            pos = getNextPositionHard(pos, index, shape, ctx.rng);
            break;
        case WalkMode3D::SOFT:
            pos = getNextPositionSoft(pos, index, shape, params, ctx.rng);
            break;
        }

        if (outPath)
            path.push_back(pos);
        ++steps;

        if (inBoundary(pos.x, pos.y, pos.z, index, shape))
            visited.insert(pos);
    }

    if (outPath)
        *outPath = std::move(path);

    return steps;
}
