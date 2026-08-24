#pragma once
#include <vector>
#include <glm/glm.hpp>

// Barnes-Hut treecode (CPU). An octree is reduced to a centre-of-mass per cell; each body
// walks the tree, using a cell as a single point mass when size/distance < theta, else
// opening it. O(N log N). The tree is flattened into a DFS-order array with a skip pointer
// per node ("next" = where to continue when a cell is not opened) so traversal is
// stackless — the identical form the GPU kernel uses (see GpuBarnesHut). Accuracy is set
// by theta (~1e-2 at 0.75, ~1e-3 at 0.5); leaves are summed exactly, so leaf size only
// trades node count for direct work, not accuracy.
namespace bh {

struct Flat {
    std::vector<glm::dvec3> com;
    std::vector<double>     mass, size;
    std::vector<int>        next, child, first, count; // child<0 => leaf
    std::vector<int>        order;                      // body indices grouped by leaf
    int                     n = 0;                      // node count
};

// Build the octree and return its flattened, stackless form (ncrit = max bodies per leaf).
Flat buildFlat(const std::vector<glm::dvec3>& pos,
               const std::vector<double>&      mass,
               int ncrit);

// CPU Barnes-Hut accelerations (drop-in for the direct solver, with an opening angle).
void accelerations(const std::vector<glm::dvec3>& pos,
                   const std::vector<double>&      mass,
                   double G, double softening, double theta,
                   std::vector<glm::dvec3>&        accOut);

} // namespace bh
