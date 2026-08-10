#pragma once
#include <glm/glm.hpp>

// The four selectable "projection" presets.
enum class ProjectionType {
    Front,        // orthographic straight-on (down -Z)
    Isometric,    // classic isometric (equal foreshortening on all three axes)
    Axonometric,  // dimetric axonometric (interpretation of "American" third-angle look)
    Oblique       // cabinet oblique (front face true, depth sheared 45 deg at half scale)
};

// Orbit camera. Also produces off-axis stereo pairs for anaglyph / stereo / VR modes.
class Camera {
public:
    glm::dvec3 target{0.0};
    double distance   = 55.0;
    double azimuth    = glm::radians(35.0);   // yaw around world Y
    double elevation  = glm::radians(22.0);   // pitch
    double fovY       = glm::radians(45.0);
    double nearP      = 0.05;
    double farP       = 8000.0;

    ProjectionType type       = ProjectionType::Isometric;
    bool           perspective = false;        // applies to Front/Iso/Axonometric

    // Stereo parameters (used by anaglyph / side-by-side / VR-SBS modes).
    double eyeSep      = 1.4;   // interocular separation in scene units
    double convergence = 55.0;  // zero-parallax distance

    void setPreset(ProjectionType t);

    glm::dvec3 dir()     const;   // unit vector from target toward the eye
    glm::dvec3 eye()     const;   // eye position
    glm::dvec3 forward() const;   // view direction (toward target)
    glm::dvec3 rightVec()const;   // camera right axis
    glm::dvec3 upVec()   const;

    glm::mat4 view() const;
    glm::mat4 proj(float aspect) const;

    // Off-axis stereo eye (parallel-axis asymmetric frustum -> always perspective).
    void stereoEye(bool leftEye, float aspect, glm::mat4& outView, glm::mat4& outProj) const;

    void clampElevation();
};
