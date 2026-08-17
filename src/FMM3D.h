#pragma once
#include <vector>
#include <glm/glm.hpp>

// Fast Multipole Method (3D Laplace/gravity) — an O(N) alternative to the direct
// O(N^2) pairwise sum. Complex solid-harmonic expansions (P2M/M2M/M2L/L2L/L2P)
// over a uniform octree, with a direct near-field. The translation operators were
// validated numerically against direct summation (relative error falls with order:
// ~9e-3 at p=2, ~1e-4 at p=6). Far-field uses the exact 1/r kernel; Plummer
// softening is applied only in the near-field, where close encounters actually
// happen (far-field pairs are well separated, so softening there is negligible).
//
// Accuracy/order guide (uniform random cloud):  p=2 ~1e-2 | p=4 ~1e-3 | p=6 ~1e-4.
// Performance: crossover with direct is around N~5000 on one core; well beyond that
// FMM wins by large margins (tens of x at N>=10^5). For the small N typical of an
// interactive scene, the direct solver is faster and remains the default.
namespace fmm {

// a_i = sum_j G*m_j*(r_j - r_i)/(|r_j-r_i|^2 + softening^2)^{3/2}
// order  : expansion order p (higher = more accurate, more expensive)
// depth  : octree depth; pass < 0 to choose automatically from N and order.
void accelerations(const std::vector<glm::dvec3>& pos,
                   const std::vector<double>&      mass,
                   double G, double softening,
                   int order,
                   std::vector<glm::dvec3>&        accOut,
                   int depth = -1);

// Auto octree depth used when depth<0 (exposed for UI/inspection).
int autoDepth(int N, int order);

} // namespace fmm
