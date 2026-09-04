#include "shapes.h"

#include <cmath>
#include <stdexcept>

const double PI = std::acos(-1);

const std::vector<std::pair<ShapeType3D, std::string>> kAllShapes3D = {
    {ShapeType3D::SPHERE, "SPHERE"},
    {ShapeType3D::OCTAHEDRON, "OCTAHEDRON"},
    {ShapeType3D::CUBE, "CUBE"},
    {ShapeType3D::RECTANGULAR_PRISM_1_2_3, "RECTANGULAR_PRISM_1_2_3"},
    {ShapeType3D::PYRAMID, "PYRAMID"},
};

std::string shapeName(ShapeType3D shape)
{
    for (const auto &[type, name] : kAllShapes3D)
        if (type == shape)
            return name;
    throw std::invalid_argument("Unknown ShapeType3D");
}

bool shapeFromName(const std::string &name, ShapeType3D &out)
{
    for (const auto &[type, shapeNameStr] : kAllShapes3D)
        if (shapeNameStr == name)
        {
            out = type;
            return true;
        }
    return false;
}

namespace
{

// Square pyramid with height equal to base side (doc 1.4 leaves the aspect
// ratio unconstrained beyond "base side and height scaled so V matches";
// this matches the convex-hull engine's implicit choice of a unit base 1x1,
// height 1 pyramid scaled uniformly, so both engines are comparable).
// Volume = (1/3) * b^2 * h = b^3/3 = (4/3) PI index^3  =>  b = index * cbrt(4*PI).
double pyramidBaseSide(int index)
{
    return static_cast<double>(index) * std::cbrt(4.0 * PI);
}

struct BBox3D
{
    double xHalf, yHalf, zHalf; // symmetric half-extents about the origin, except PYRAMID (z in [0, h])
};

} // namespace

bool inBoundary(int x, int y, int z, int index, ShapeType3D shape)
{
    switch (shape)
    {
    case ShapeType3D::SPHERE:
        return (x * x + y * y + z * z) <= (index * index);

    case ShapeType3D::OCTAHEDRON:
    {
        double side = index * std::pow(std::sqrt(8.0) * PI, 1.0 / 3.0);
        return (std::abs(x) + std::abs(y) + std::abs(z)) <= side / std::sqrt(2.0);
    }

    case ShapeType3D::CUBE:
    {
        double constant = std::pow(4.0 * PI / 3.0, 1.0 / 3.0) * index;
        double halfSide = constant / 2.0;
        return (std::abs(x) <= halfSide) && (std::abs(y) <= halfSide) && (std::abs(z) <= halfSide);
    }

    case ShapeType3D::RECTANGULAR_PRISM_1_2_3:
    {
        double constant = std::pow(2.0 * PI / 9.0, 1.0 / 3.0) * index;
        double halfX = constant / 2.0;
        double halfY = constant;
        double halfZ = 1.5 * constant;
        return (std::abs(x) <= halfX) && (std::abs(y) <= halfY) && (std::abs(z) <= halfZ);
    }

    case ShapeType3D::PYRAMID:
    {
        double b = pyramidBaseSide(index);
        double h = b;
        double halfB = b / 2.0;
        if (z < 0 || z > h)
            return false;
        double taper = halfB * (1.0 - z / h);
        return (std::abs(x) <= taper) && (std::abs(y) <= taper);
    }
    }

    throw std::invalid_argument("Unknown ShapeType3D in inBoundary");
}

namespace
{

BBox3D computeBBox(ShapeType3D shape, int index)
{
    constexpr double MARGIN = 1.0;
    switch (shape)
    {
    case ShapeType3D::SPHERE:
        return {static_cast<double>(index) + MARGIN, static_cast<double>(index) + MARGIN, static_cast<double>(index) + MARGIN};

    case ShapeType3D::OCTAHEDRON:
    {
        double constant = index * std::pow(std::sqrt(8.0) * PI, 1.0 / 3.0) / std::sqrt(2.0);
        return {constant + MARGIN, constant + MARGIN, constant + MARGIN};
    }

    case ShapeType3D::CUBE:
    {
        double halfSide = std::pow(4.0 * PI / 3.0, 1.0 / 3.0) * index / 2.0;
        return {halfSide + MARGIN, halfSide + MARGIN, halfSide + MARGIN};
    }

    case ShapeType3D::RECTANGULAR_PRISM_1_2_3:
    {
        double constant = std::pow(2.0 * PI / 9.0, 1.0 / 3.0) * index;
        return {constant / 2.0 + MARGIN, constant + MARGIN, 1.5 * constant + MARGIN};
    }

    case ShapeType3D::PYRAMID:
    {
        double b = pyramidBaseSide(index);
        return {b / 2.0 + MARGIN, b / 2.0 + MARGIN, b + MARGIN}; // z handled specially (0..h)
    }
    }
    throw std::invalid_argument("Unknown ShapeType3D in computeBBox");
}

} // namespace

void boundingBox(int index, ShapeType3D shape, int &xMin, int &xMax, int &yMin, int &yMax, int &zMin, int &zMax)
{
    BBox3D box = computeBBox(shape, index);

    xMin = -static_cast<int>(std::floor(box.xHalf)) - 1;
    yMin = -static_cast<int>(std::floor(box.yHalf)) - 1;
    zMin = (shape == ShapeType3D::PYRAMID) ? -1 : -static_cast<int>(std::floor(box.zHalf)) - 1;

    xMax = static_cast<int>(std::ceil(box.xHalf)) + 1;
    yMax = static_cast<int>(std::ceil(box.yHalf)) + 1;
    zMax = static_cast<int>(std::ceil(box.zHalf)) + 1;
}

int getPointsInShape(int index, ShapeType3D shape)
{
    int xfloor, xcell, yfloor, ycell, zfloor, zcell;
    boundingBox(index, shape, xfloor, xcell, yfloor, ycell, zfloor, zcell);

    int points = 0;
    for (int i = xfloor; i < xcell; ++i)
        for (int j = yfloor; j < ycell; ++j)
            for (int k = zfloor; k < zcell; ++k)
                if (inBoundary(i, j, k, index, shape))
                    ++points;

    return points;
}

void initialize(LatticePoint3D &start, ShapeType3D shape, int index, std::mt19937_64 &rng)
{
    BBox3D box = computeBBox(shape, index);

    double zfloor = (shape == ShapeType3D::PYRAMID) ? -1.0 : -box.zHalf;
    double zceil = box.zHalf;

    std::uniform_int_distribution<> distX(-static_cast<int>(std::ceil(box.xHalf)), static_cast<int>(std::ceil(box.xHalf)));
    std::uniform_int_distribution<> distY(-static_cast<int>(std::ceil(box.yHalf)), static_cast<int>(std::ceil(box.yHalf)));
    std::uniform_int_distribution<> distZ(static_cast<int>(std::floor(zfloor)), static_cast<int>(std::ceil(zceil)));

    do
    {
        start.x = distX(rng);
        start.y = distY(rng);
        start.z = distZ(rng);
    } while (!inBoundary(start.x, start.y, start.z, index, shape));
}
