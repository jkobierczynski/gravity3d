#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "Body.h"

// Owns GL resources (sphere mesh, trail buffer, shaders) and draws the scene for one eye.
class Renderer {
public:
    bool init();
    void shutdown();

    // Draw every body as a shaded sphere using the given view/projection and headlight.
    void drawBodies(const glm::mat4& view, const glm::mat4& proj,
                    const std::vector<Body>& bodies, const glm::vec3& lightPos);

    // Draw one polyline (a body's trail). 'verts' holds interleaved x,y,z,alpha.
    void drawTrail(const glm::mat4& view, const glm::mat4& proj,
                   const glm::vec3& color, const std::vector<float>& verts);

    // Draw a screen-space line strip from interleaved x,y pixel pairs (for panel dividers).
    void drawScreenLines(int screenW, int screenH,
                         const glm::vec3& color, const std::vector<float>& xy);

    // Draw bitmap text at pixel (x,y) (top-left origin), scaled, for panel labels.
    void drawText2D(int screenW, int screenH, float x, float y, float scale,
                    const glm::vec3& color, const char* text);

private:
    unsigned int compile(const char* vs, const char* fs);
    void buildSphere(int stacks, int slices);

    // Body sphere
    unsigned int bodyProg_ = 0, sphereVAO_ = 0, sphereVBO_ = 0, sphereEBO_ = 0;
    int sphereIndexCount_ = 0;
    int uModel_, uView_, uProj_, uNormalMat_, uColor_, uLightPos_;

    // Trails
    unsigned int lineProg_ = 0, lineVAO_ = 0, lineVBO_ = 0;
    int lView_, lProj_, lColor_;
    size_t lineVBOCapacity_ = 0;

    // 2D overlay (labels + dividers)
    unsigned int textProg_ = 0, textVAO_ = 0, textVBO_ = 0, textEBO_ = 0;
    int tProj_, tColor_;
    size_t textVBOCap_ = 0, textEBOCap_ = 0;
};
