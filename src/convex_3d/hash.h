#ifndef CONVEX_3D_HASH_H
#define CONVEX_3D_HASH_H

// A previous revision of the 3D lattice-point set used a hand-rolled
// separate-chaining hashmap with a broken hash function:
//   hashCode = (hx << 32 | hy << 16 | hz) % HashSize
// Shifting hy by only 16 bits (instead of, say, 21) lets hy's upper bits
// collide with hx's lower bits, and hz is packed unshifted into the same 16
// bits hy occupies -- distinct (x, y, z) triples routinely collide. This
// replaces it with the same unordered_set-based approach already used by
// the 2D engine (see convex_2d/hash.h), which is both correct and simpler.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_set>

struct LatticePoint3D
{
    int x;
    int y;
    int z;

    LatticePoint3D() : x(0), y(0), z(0) {}
    LatticePoint3D(int xInput, int yInput, int zInput) : x(xInput), y(yInput), z(zInput) {}

    bool operator==(const LatticePoint3D &other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

inline std::size_t hashThreeInts(int x, int y, int z)
{
    std::uint64_t h1 = std::hash<int>{}(x);
    std::uint64_t h2 = std::hash<int>{}(y);
    std::uint64_t h3 = std::hash<int>{}(z);
    // Boost-style hash_combine mixing.
    std::uint64_t seed = h1;
    seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    seed ^= h3 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return static_cast<std::size_t>(seed);
}

struct LatticePoint3DHash
{
    std::size_t operator()(const LatticePoint3D &p) const noexcept
    {
        return hashThreeInts(p.x, p.y, p.z);
    }
};

using PointSet3D = std::unordered_set<LatticePoint3D, LatticePoint3DHash>;

#endif // CONVEX_3D_HASH_H
