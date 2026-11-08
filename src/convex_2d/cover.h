#ifndef CONVEX_2D_COVER_H
#define CONVEX_2D_COVER_H

// Disk-covering analysis (doc section 4.4). Whenever the walker steps onto a
// lattice point v that is not yet covered by any existing disk, a covering
// disk of radius R is placed at a real-valued center c drawn uniformly from
// the disk of radius R around v; every shape-interior lattice point within
// Euclidean distance R of c becomes "covered". Since |c - v| <= R, v itself
// is always covered. c is free to fall outside the shape -- only the
// shape's lattice points ever count as covered. Points already inside an
// existing disk get no new disk. The quantity of interest phi is the number
// of disks placed in a run (equation 12), averaged over runs.
//
// An earlier revision placed a disk on *every* first visit and reported the
// coverage fraction |C_t| / |L(S)|. Under the 50% visit stopping rule that
// made the disk count identical to the distinct-visited count, so it carried
// no information; the doc's equation (12) was corrected accordingly.

#include <random>
#include <vector>

#include "hash.h"
#include "shapes.h"

// A disk center: unlike the walker's LatticePoint2D, it can sit anywhere
// in the plane.
struct DiskCenter2D
{
    double x;
    double y;
};

class DiskCover2D
{
public:
    // recordCenters: keep every placed center for centers(). Only the one
    // sampled run per index that gets written to cover_*.txt needs this.
    DiskCover2D(double radius, ShapeType2D shape, int index, bool recordCenters = false);

    // If lattice point (vx, vy) is not already covered, places a disk at a
    // uniformly random center within radius R of it and returns true.
    bool tryPlaceDisk(int vx, int vy, std::mt19937_64 &rng);

    // phi for this run, per doc equation (12).
    int diskCount() const { return diskCount_; }

    // Centers of every disk placed so far, in placement order (empty unless
    // constructed with recordCenters).
    const std::vector<DiskCenter2D> &centers() const { return centers_; }

    std::size_t coveredCount() const { return covered_.size(); }
    double radius() const { return r_; }

private:
    double r_;
    double r2_;
    ShapeType2D shape_;
    int index_;
    int diskCount_ = 0;
    bool recordCenters_;
    std::vector<DiskCenter2D> centers_;
    PointSet2D covered_;
};

#endif // CONVEX_2D_COVER_H
