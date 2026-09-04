#ifndef CONVEX_2D_COVER_H
#define CONVEX_2D_COVER_H

// Disk-covering analysis (doc section 1.10). Each time the walker visits a
// lattice point v for the first time, a covering disk D(v, R) is placed
// exactly at v; every shape-interior lattice point within Euclidean distance
// R of v becomes "covered". The coverage fraction phi = |C_t| / |L(S)| is
// the quantity of interest (equation 12).
//
// An earlier revision of this class placed the disk at a uniformly random
// point *within* radius R of (x, y) rather than at (x, y) itself, and did
// not restrict covered points to the shape's own interior -- neither of
// which matches the documented definition. Both are fixed here.

#include "hash.h"
#include "shapes.h"

class DiskCover2D
{
public:
    DiskCover2D(double radius, ShapeType2D shape, int index);

    // Call once, the first time the walk visits lattice point (vx, vy).
    void coverPoint(int vx, int vy);

    // phi = |covered| / |L(S)|, per doc equation (12).
    double coverageFraction(long long totalShapePoints) const;

    std::size_t coveredCount() const { return covered_.size(); }
    double radius() const { return r_; }

private:
    double r_;
    double r2_;
    ShapeType2D shape_;
    int index_;
    PointSet2D covered_;
};

#endif // CONVEX_2D_COVER_H
