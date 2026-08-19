#pragma once
#include <vector>
#include <glm/glm.hpp>

// GPU direct O(N^2) gravity solver via an OpenGL 4.3 compute shader.
//
// Vendor-independent (any GL 4.3+ driver: NVIDIA, AMD, Intel). It computes the same
// softened pairwise sum as Simulation's CPU path, but in fp32 on the GPU — so it is a
// fast *float* path, not bit-identical to the CPU double solver. Expect ~1e-5..1e-6
// relative error vs the double sum; the CPU direct solver remains the ground truth.
//
// init() must be called once AFTER a GL 4.3 core context exists (i.e. after the window
// and GLAD are up). If the context is < 4.3 or the shader fails to build, init() returns
// false and available() stays false; callers should fall back to the CPU path.
namespace gpu {

bool init();          // compile program + create buffers; false if unsupported
bool available();     // true once init() succeeded
const char* deviceInfo();   // GL_RENDERER string (for a startup line), or ""

// a_i = sum_j G*m_j*(r_j - r_i)/(|r_j-r_i|^2 + softening^2)^{3/2}, computed on the GPU.
void accelerations(const std::vector<glm::dvec3>& pos,
                   const std::vector<double>&      mass,
                   double G, double softening,
                   std::vector<glm::dvec3>&        accOut);

void shutdown();
} // namespace gpu
