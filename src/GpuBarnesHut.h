#pragma once
#include <vector>
#include <glm/glm.hpp>

// GPU Barnes-Hut treecode via an OpenGL 4.3 compute shader.
//
// The CPU builds and flattens the octree each step (cheap, O(N)); the GPU does the
// expensive part — one invocation per body walks the flattened, stackless node array
// (skip pointer per cell), using a cell as a point mass when size/distance < theta and
// opening it otherwise. Same traversal as bh::accelerations, so the CPU path is its
// reference. fp32 on the GPU; vendor-independent (any GL 4.3+ driver).
//
// init() must be called once after a GL 4.3 core context exists; if unsupported it
// returns false and callers should fall back to bh::accelerations (CPU).
namespace gpubh {

bool init();
bool available();
const char* deviceInfo();

void accelerations(const std::vector<glm::dvec3>& pos,
                   const std::vector<double>&      mass,
                   double G, double softening, double theta,
                   std::vector<glm::dvec3>&        accOut);

void shutdown();
} // namespace gpubh
