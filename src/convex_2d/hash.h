#ifndef CONVEX_2D_HASH_H
#define CONVEX_2D_HASH_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_set>

struct LatticePoint2D
{
    int x;
    int y;

    LatticePoint2D() : x(0), y(0) {}
    LatticePoint2D(int xInput, int yInput) : x(xInput), y(yInput) {}

    bool operator==(const LatticePoint2D &other) const
    {
        return x == other.x && y == other.y;
    }
};

// Packs both coordinates losslessly into one 64-bit word (each int occupies
// its own 32-bit half) and defers to std::hash for the final mix.
inline std::size_t hashTwoInts(int x, int y)
{
    std::uint64_t packed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
                            static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
    return std::hash<std::uint64_t>{}(packed);
}

struct LatticePoint2DHash
{
    std::size_t operator()(const LatticePoint2D &p) const noexcept
    {
        return hashTwoInts(p.x, p.y);
    }
};

using PointSet2D = std::unordered_set<LatticePoint2D, LatticePoint2DHash>;

#endif // CONVEX_2D_HASH_H
