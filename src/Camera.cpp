#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

void Camera::clampElevation() {
    const double lim = glm::radians(89.0);
    elevation = std::max(-lim, std::min(lim, elevation));
}

void Camera::setPreset(ProjectionType t) {
    type = t;
    switch (t) {
        case ProjectionType::Front:
            azimuth = 0.0; elevation = 0.0; perspective = false; break;
        case ProjectionType::Isometric:
            azimuth = glm::radians(45.0);
            elevation = std::atan(1.0 / std::sqrt(2.0)); // ~35.264 deg
            perspective = false; break;
        case ProjectionType::Axonometric:
            azimuth = glm::radians(45.0);
            elevation = glm::radians(20.0);
            perspective = false; break;
        case ProjectionType::Oblique:
            azimuth = 0.0; elevation = 0.0; perspective = false; break;
    }
}

glm::dvec3 Camera::dir() const {
    double ce = std::cos(elevation), se = std::sin(elevation);
    return glm::dvec3(ce * std::sin(azimuth), se, ce * std::cos(azimuth));
}
glm::dvec3 Camera::eye()     const { return target + distance * dir(); }
glm::dvec3 Camera::forward() const { return glm::normalize(target - eye()); }
glm::dvec3 Camera::upVec()   const { return glm::dvec3(0.0, 1.0, 0.0); }
glm::dvec3 Camera::rightVec()const { return glm::normalize(glm::cross(forward(), upVec())); }

glm::mat4 Camera::view() const {
    return glm::lookAt(glm::vec3(eye()), glm::vec3(target), glm::vec3(upVec()));
}

glm::mat4 Camera::proj(float aspect) const {
    if (type == ProjectionType::Oblique) {
        // Cabinet oblique: orthographic front view with a depth shear (45 deg, half scale).
        float s = static_cast<float>(distance * 0.5);
        glm::mat4 ortho = glm::ortho(-s * aspect, s * aspect, -s, s,
                                     static_cast<float>(nearP), static_cast<float>(farP));
        glm::mat4 shear(1.0f);
        float k = 0.5f * std::cos(glm::radians(45.0f)); // ~0.3536
        shear[2][0] = k;   // x += k * z_view
        shear[2][1] = k;   // y += k * z_view
        return ortho * shear;
    }
    if (perspective) {
        return glm::perspective(static_cast<float>(fovY), aspect,
                                static_cast<float>(nearP), static_cast<float>(farP));
    }
    float s = static_cast<float>(distance * 0.5);
    return glm::ortho(-s * aspect, s * aspect, -s, s,
                      static_cast<float>(nearP), static_cast<float>(farP));
}

void Camera::stereoEye(bool leftEye, float aspect,
                       glm::mat4& outView, glm::mat4& outProj) const {
    glm::dvec3 center = eye();
    glm::dvec3 fwd    = forward();
    glm::dvec3 up     = upVec();
    glm::dvec3 rgt    = rightVec();

    double half = eyeSep * 0.5;
    glm::dvec3 eyePos = leftEye ? center - rgt * half
                                : center + rgt * half;

    // Parallel-axis: both eyes look along the same forward direction.
    outView = glm::lookAt(glm::vec3(eyePos), glm::vec3(eyePos + fwd), glm::vec3(up));

    double t = nearP * std::tan(fovY * 0.5);
    double a = aspect * t;
    double b = 0.5 * eyeSep * nearP / convergence;   // horizontal frustum shift

    double l, r;
    if (leftEye) { l = -a + b; r =  a + b; }
    else         { l = -a - b; r =  a - b; }

    outProj = glm::frustum((float)l, (float)r, (float)(-t), (float)t,
                           (float)nearP, (float)farP);
}
