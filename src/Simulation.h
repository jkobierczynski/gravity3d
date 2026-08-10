#pragma once
#include <vector>
#include "Body.h"

// N-body gravity integrator (velocity Verlet, O(n^2), Plummer softening).
class Simulation {
public:
    double G          = 1.0;    // gravitational constant (scene units)
    double softening  = 0.05;   // Plummer softening length (avoids singularities)
    double dt         = 0.004;  // fixed internal timestep
    double timeScale  = 1.0;    // simulation-speed multiplier
    bool   paused     = false;

    // Store the initial state (optionally remove any net drift of the whole system).
    void setInitial(const std::vector<Body>& bodies, bool zeroMomentum);

    // Restore the exact initial state.
    void reset();

    // Advance the simulation by 'frameSeconds' of wall-clock time (scaled by timeScale),
    // using fixed dt substeps for stability. Does nothing while paused.
    void step(double frameSeconds);

    const std::vector<Body>& bodies() const { return current_; }
    std::vector<Body>&       bodies()       { return current_; }
    double simTime() const { return t_; }

private:
    void computeAccelerations(const std::vector<Body>& b, std::vector<glm::dvec3>& outAcc) const;
    void integrate(double h);

    std::vector<Body> initial_;
    std::vector<Body> current_;
    double t_           = 0.0;
    double accumulator_ = 0.0;
};
