#pragma once
#include <string>
#include <glm/glm.hpp>

// A single gravitating object in the scene.
struct Body {
    std::string name;
    double      mass   = 1.0;
    glm::dvec3  pos{0.0};        // position
    glm::dvec3  vel{0.0};        // velocity
    glm::dvec3  acc{0.0};        // current acceleration (kept for Verlet integration)
    glm::vec3   color{1.0f};     // RGB in 0..1
    double      radius = 0.0;    // visual radius; if <= 0 it is derived from mass

    // Visual radius actually used for drawing.
    float visualRadius() const {
        if (radius > 0.0) return static_cast<float>(radius);
        return static_cast<float>(std::max(0.15, 0.25 * std::cbrt(std::max(mass, 1e-9))));
    }
};
