#include "Simulation.h"
#include "FMM3D.h"
#include <algorithm>

void Simulation::setInitial(const std::vector<Body>& bodies, bool zeroMomentum) {
    initial_ = bodies;

    if (zeroMomentum && !initial_.empty()) {
        glm::dvec3 p{0.0};
        double m = 0.0;
        for (const auto& b : initial_) { p += b.mass * b.vel; m += b.mass; }
        if (m > 0.0) {
            glm::dvec3 vcm = p / m;                 // centre-of-mass velocity
            for (auto& b : initial_) b.vel -= vcm;  // subtract it so the system stays put
        }
    }
    reset();
}

void Simulation::reset() {
    current_     = initial_;
    t_           = 0.0;
    accumulator_ = 0.0;
    std::vector<glm::dvec3> a;
    computeAccelerations(current_, a);              // prime accelerations for Verlet
    for (size_t i = 0; i < current_.size(); ++i) current_[i].acc = a[i];
}

void Simulation::computeAccelerations(const std::vector<Body>& b,
                                      std::vector<glm::dvec3>& outAcc) const {
    const size_t n = b.size();

    if (useFMM && n > 0) {
        // Fast Multipole Method path (O(N)). Far-field uses the exact kernel;
        // Plummer softening is applied in the near-field, matching the direct sum.
        std::vector<glm::dvec3> pos(n);
        std::vector<double>     mass(n);
        for (size_t i = 0; i < n; ++i) { pos[i] = b[i].pos; mass[i] = b[i].mass; }
        fmm::accelerations(pos, mass, G, softening, fmmOrder, outAcc);
        return;
    }

    // Direct O(n^2) pairwise sum (default / ground truth).
    outAcc.assign(n, glm::dvec3(0.0));
    const double eps2 = softening * softening;
    for (size_t i = 0; i < n; ++i) {
        glm::dvec3 ai(0.0);
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            glm::dvec3 d = b[j].pos - b[i].pos;
            double r2  = d.x * d.x + d.y * d.y + d.z * d.z + eps2;
            double inv = 1.0 / std::sqrt(r2);
            double inv3 = inv * inv * inv;
            ai += (G * b[j].mass * inv3) * d;
        }
        outAcc[i] = ai;
    }
}

void Simulation::integrate(double h) {
    const size_t n = current_.size();
    // x(t+h) = x + v*h + 0.5*a*h^2
    for (size_t i = 0; i < n; ++i)
        current_[i].pos += current_[i].vel * h + 0.5 * current_[i].acc * (h * h);

    std::vector<glm::dvec3> aNew;
    computeAccelerations(current_, aNew);

    // v(t+h) = v + 0.5*(a + a_new)*h
    for (size_t i = 0; i < n; ++i) {
        current_[i].vel += 0.5 * (current_[i].acc + aNew[i]) * h;
        current_[i].acc  = aNew[i];
    }
    t_ += h;
}

void Simulation::step(double frameSeconds) {
    if (paused || current_.empty()) return;
    accumulator_ += frameSeconds * timeScale;

    // Cap substeps so a long stall can't trigger a spiral of death.
    int guard = 0;
    const int maxSub = 4000;
    while (accumulator_ >= dt && guard < maxSub) {
        integrate(dt);
        accumulator_ -= dt;
        ++guard;
    }
    if (guard >= maxSub) accumulator_ = 0.0;
}
