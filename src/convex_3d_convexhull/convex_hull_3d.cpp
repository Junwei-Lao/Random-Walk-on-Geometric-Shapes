#include "convex_hull_3d.h"

#include <CGAL/Bbox_3.h>
#include <CGAL/Side_of_triangle_mesh.h>
#include <CGAL/boost/graph/copy_face_graph.h>
#include <CGAL/convex_hull_3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace PMP = CGAL::Polygon_mesh_processing;

const double PI = std::acos(-1);

const std::vector<std::pair<ShapeType3DHull, std::string>> kAllShapes3DHull = {
    {ShapeType3DHull::OCTAHEDRON, "OCTAHEDRON"},
    {ShapeType3DHull::CUBE, "CUBE"},
    {ShapeType3DHull::RECTANGULAR_PRISM_1_2_3, "RECTANGULAR_PRISM_1_2_3"},
    {ShapeType3DHull::PYRAMID, "PYRAMID"},
};

std::string shapeName(ShapeType3DHull shape)
{
    for (const auto &[type, name] : kAllShapes3DHull)
        if (type == shape)
            return name;
    throw std::invalid_argument("Unknown ShapeType3DHull");
}

bool shapeFromName(const std::string &name, ShapeType3DHull &out)
{
    for (const auto &[type, shapeNameStr] : kAllShapes3DHull)
        if (shapeNameStr == name)
        {
            out = type;
            return true;
        }
    return false;
}

namespace
{

std::vector<Point3> baseVertices(ShapeType3DHull shape)
{
    switch (shape)
    {
    case ShapeType3DHull::OCTAHEDRON:
        // Regular octahedron inscribed in a 1x1x1 box.
        return {
            Point3(0.5, 0.5, 0.0), Point3(0.5, 0.5, 1.0),
            Point3(0.5, 0.0, 0.5), Point3(0.5, 1.0, 0.5),
            Point3(0.0, 0.5, 0.5), Point3(1.0, 0.5, 0.5),
        };
    case ShapeType3DHull::CUBE:
        return {
            Point3(0.0, 0.0, 0.0), Point3(1.0, 0.0, 0.0), Point3(1.0, 1.0, 0.0), Point3(0.0, 1.0, 0.0),
            Point3(0.0, 0.0, 1.0), Point3(1.0, 0.0, 1.0), Point3(1.0, 1.0, 1.0), Point3(0.0, 1.0, 1.0),
        };
    case ShapeType3DHull::RECTANGULAR_PRISM_1_2_3:
        return {
            Point3(0.0, 0.0, 0.0), Point3(1.0, 0.0, 0.0), Point3(1.0, 2.0, 0.0), Point3(0.0, 2.0, 0.0),
            Point3(0.0, 0.0, 3.0), Point3(1.0, 0.0, 3.0), Point3(1.0, 2.0, 3.0), Point3(0.0, 2.0, 3.0),
        };
    case ShapeType3DHull::PYRAMID:
        // Square pyramid, base 1x1, height 1 (matches convex_3d/shapes.cpp's
        // analytic PYRAMID, which also uses height == base side).
        return {
            Point3(0.0, 0.0, 0.0), Point3(1.0, 0.0, 0.0), Point3(1.0, 1.0, 0.0), Point3(0.0, 1.0, 0.0),
            Point3(0.5, 0.5, 1.0),
        };
    }
    throw std::invalid_argument("Unknown ShapeType3DHull in baseVertices");
}

} // namespace

void ConvexHullDomain3D::generateBaseHull(ShapeType3DHull shape)
{
    std::vector<Point3> pts = baseVertices(shape);

    Mesh hull;
    CGAL::convex_hull_3(pts.begin(), pts.end(), hull);

    if (!CGAL::is_closed(hull))
        throw std::runtime_error("Convex hull mesh is not closed");

    double vol = std::abs(PMP::volume(hull));
    if (!(vol > 0.0))
        throw std::runtime_error("Base hull has non-positive volume");

    mesh_ = std::move(hull);
    baseVolume_ = vol;
    actualVolume_ = vol;
}

ConvexHullDomain3D ConvexHullDomain3D::scaledCopyForVolume(double targetVolume) const
{
    if (mesh_.number_of_vertices() == 0)
        throw std::runtime_error("Base hull is empty. Call generateBaseHull first.");
    if (targetVolume <= 0.0)
        throw std::invalid_argument("targetVolume must be positive");

    ConvexHullDomain3D out;
    CGAL::copy_face_graph(mesh_, out.mesh_);

    double scale = std::cbrt(targetVolume / baseVolume_);
    for (auto v : out.mesh_.vertices())
    {
        const Point3 &p = out.mesh_.point(v);
        out.mesh_.point(v) = Point3(scale * p.x(), scale * p.y(), scale * p.z());
    }

    out.baseVolume_ = baseVolume_;
    out.actualVolume_ = std::abs(PMP::volume(out.mesh_));
    return out;
}

void ConvexHullDomain3D::buildIntegerGridPoints()
{
    insidePoints_.clear();
    insideSet_.clear();

    if (mesh_.number_of_vertices() == 0)
        throw std::runtime_error("Mesh is empty.");

    CGAL::Bbox_3 bbox = CGAL::bbox_3(mesh_.points().begin(), mesh_.points().end());

    int xmin = static_cast<int>(std::ceil(bbox.xmin()));
    int xmax = static_cast<int>(std::floor(bbox.xmax()));
    int ymin = static_cast<int>(std::ceil(bbox.ymin()));
    int ymax = static_cast<int>(std::floor(bbox.ymax()));
    int zmin = static_cast<int>(std::ceil(bbox.zmin()));
    int zmax = static_cast<int>(std::floor(bbox.zmax()));

    CGAL::Side_of_triangle_mesh<Mesh, Kernel> insideTester(mesh_);

    for (int x = xmin; x <= xmax; ++x)
    {
        for (int y = ymin; y <= ymax; ++y)
        {
            for (int z = zmin; z <= zmax; ++z)
            {
                Point3 q(x, y, z);
                auto side = insideTester(q);
                if (side == CGAL::ON_BOUNDED_SIDE || side == CGAL::ON_BOUNDARY)
                {
                    GridPoint3D p{x, y, z};
                    insidePoints_.push_back(p);
                    insideSet_.insert(p);
                }
            }
        }
    }

    if (insidePoints_.empty())
        throw std::runtime_error("No integer grid points found inside the hull.");
}

std::uint64_t RandomWalkSimulator3D::runOneTrial(const ConvexHullDomain3D &domain, double coverageFraction)
{
    const auto &pts = domain.points();
    const std::size_t total = pts.size();
    if (total == 0)
        throw std::runtime_error("Domain has no integer points.");
    if (!(coverageFraction > 0.0 && coverageFraction <= 1.0))
        throw std::invalid_argument("coverageFraction must be in (0, 1]");

    const std::size_t targetUnique =
        static_cast<std::size_t>(std::ceil(coverageFraction * static_cast<double>(total)));

    std::uniform_int_distribution<std::size_t> startDist(0, total - 1);
    static const std::array<GridPoint3D, 6> dirs{{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    }};

    GridPoint3D current = pts[startDist(rng_)];
    std::unordered_set<GridPoint3D, GridPoint3DHash> visited;
    visited.reserve(targetUnique * 2 + 16);
    visited.insert(current);

    // Doc section 1.5: choose uniformly among the *valid* neighbors (see
    // convex_hull_3d.h header comment for why this replaced the original
    // "uniform over all 6 dirs, stay in place if invalid" self-loop walk).
    std::array<GridPoint3D, 6> candidates;
    std::uint64_t steps = 0;
    while (visited.size() < targetUnique)
    {
        int numValid = 0;
        for (const auto &d : dirs)
        {
            GridPoint3D next{current.x + d.x, current.y + d.y, current.z + d.z};
            if (domain.contains(next))
                candidates[numValid++] = next;
        }
        if (numValid > 0)
        {
            std::uniform_int_distribution<int> pick(0, numValid - 1);
            current = candidates[pick(rng_)];
        }
        visited.insert(current);
        ++steps;
    }
    return steps;
}

Stats3D RandomWalkSimulator3D::runManyTrials(const ConvexHullDomain3D &domain, double coverageFraction, int trials)
{
    if (trials < 2)
        throw std::invalid_argument("trials must be at least 2");

    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(trials));
    for (int i = 0; i < trials; ++i)
        values.push_back(static_cast<double>(runOneTrial(domain, coverageFraction)));

    double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    double accum = 0.0;
    for (double v : values)
    {
        double d = v - mean;
        accum += d * d;
    }
    double variance = accum / static_cast<double>(values.size() - 1);
    return Stats3D{mean, std::sqrt(variance)};
}
