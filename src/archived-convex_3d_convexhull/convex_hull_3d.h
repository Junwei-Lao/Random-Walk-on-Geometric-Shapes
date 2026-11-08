#ifndef CONVEX_HULL_3D_H
#define CONVEX_HULL_3D_H

// Builds each 3D convex shape from an explicit vertex set, takes its convex
// hull via CGAL, and rasterizes interior integer grid points via an
// inside/outside mesh test -- a generic, formula-free alternative to
// convex_3d/shapes.cpp's closed-form inBoundary formulas. Requires CGAL.
//
// This is a refactor of the original single-file random_walk_convex_hull.cpp
// into a reusable library + CLI split (see main.cpp), consistent with the
// project's other three engines. Two behavioral fixes from the original:
//   - The random walk now chooses uniformly among *valid* neighbors (doc
//     section 1.5), rather than uniformly among all 6 directions with a
//     "stay in place" self-loop whenever the chosen direction was invalid --
//     the latter biases mixing time upward near the boundary and didn't
//     match the analytic engines' walk rule.
//   - Trials now run multithreaded, matching the other three engines.
// The unused random-vertex and Fibonacci-sphere hull generators (present in
// the original file but never exercised by its main()) have been dropped.

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polygon_mesh_processing/measure.h>
#include <CGAL/Surface_mesh.h>

#include <cstdint>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

extern const double PI;

enum class ShapeType3DHull
{
    OCTAHEDRON,
    CUBE,
    RECTANGULAR_PRISM_1_2_3,
    PYRAMID
};

extern const std::vector<std::pair<ShapeType3DHull, std::string>> kAllShapes3DHull;

std::string shapeName(ShapeType3DHull shape);
bool shapeFromName(const std::string &name, ShapeType3DHull &out);

using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point3 = Kernel::Point_3;
using Mesh = CGAL::Surface_mesh<Point3>;

struct GridPoint3D
{
    int x{};
    int y{};
    int z{};

    bool operator==(const GridPoint3D &other) const { return x == other.x && y == other.y && z == other.z; }
};

struct GridPoint3DHash
{
    std::size_t operator()(const GridPoint3D &p) const noexcept
    {
        std::size_t h1 = std::hash<int>{}(p.x);
        std::size_t h2 = std::hash<int>{}(p.y);
        std::size_t h3 = std::hash<int>{}(p.z);
        return h1 ^ (h2 << 1) ^ (h3 << 7);
    }
};

class ConvexHullDomain3D
{
public:
    // Builds the base hull for `shape` at unit scale. Throws
    // std::runtime_error on failure.
    void generateBaseHull(ShapeType3DHull shape);

    // Uniformly scales the base hull so its volume matches `targetVolume`.
    ConvexHullDomain3D scaledCopyForVolume(double targetVolume) const;

    // Rasterizes all integer lattice points inside/on the (possibly scaled)
    // hull mesh. Must be called before points()/contains().
    void buildIntegerGridPoints();

    bool contains(const GridPoint3D &p) const { return insideSet_.count(p) != 0; }
    const std::vector<GridPoint3D> &points() const { return insidePoints_; }
    std::size_t gridPointCount() const { return insidePoints_.size(); }

    double baseVolume() const { return baseVolume_; }
    double actualVolume() const { return actualVolume_; }

private:
    Mesh mesh_;
    double baseVolume_ = 0.0;
    double actualVolume_ = 0.0;

    std::vector<GridPoint3D> insidePoints_;
    std::unordered_set<GridPoint3D, GridPoint3DHash> insideSet_;
};

struct Stats3D
{
    double mean = 0.0;
    double sampleStddev = 0.0;
};

class RandomWalkSimulator3D
{
public:
    explicit RandomWalkSimulator3D(std::uint64_t seed) : rng_(seed) {}

    // Random-walks on `domain` (hard boundary, doc section 1.5/1.6) until
    // `coverageFraction` of its grid points have been visited; returns step count.
    std::uint64_t runOneTrial(const ConvexHullDomain3D &domain, double coverageFraction);

    Stats3D runManyTrials(const ConvexHullDomain3D &domain, double coverageFraction, int trials);

private:
    std::mt19937_64 rng_;
};

#endif // CONVEX_HULL_3D_H
