#include "Renderer.h"
#include "Shaders.h"
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdio>
#include <vector>
#include "third_party/stb/stb_easy_font.h"

unsigned int Renderer::compile(const char* vs, const char* fs) {
    auto make = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log);
                   std::fprintf(stderr, "Shader compile error:\n%s\n", log); }
        return s;
    };
    GLuint v = make(GL_VERTEX_SHADER, vs);
    GLuint f = make(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log);
               std::fprintf(stderr, "Program link error:\n%s\n", log); }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

void Renderer::buildSphere(int stacks, int slices) {
    std::vector<float> verts;   // pos(3) + normal(3)
    std::vector<unsigned int> idx;
    for (int i = 0; i <= stacks; ++i) {
        float phi = (float)M_PI * (float)i / (float)stacks;          // 0..pi
        float y = std::cos(phi), r = std::sin(phi);
        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * (float)M_PI * (float)j / (float)slices;
            float x = r * std::cos(theta), z = r * std::sin(theta);
            verts.insert(verts.end(), { x, y, z, x, y, z }); // unit sphere: normal == pos
        }
    }
    int cols = slices + 1;
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            unsigned int a = i * cols + j, b = (i + 1) * cols + j;
            idx.insert(idx.end(), { a, b, a + 1, a + 1, b, b + 1 });
        }
    sphereIndexCount_ = (int)idx.size();

    glGenVertexArrays(1, &sphereVAO_);
    glGenBuffers(1, &sphereVBO_);
    glGenBuffers(1, &sphereEBO_);
    glBindVertexArray(sphereVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

bool Renderer::init() {
    bodyProg_ = compile(kBodyVert, kBodyFrag);
    uModel_     = glGetUniformLocation(bodyProg_, "uModel");
    uView_      = glGetUniformLocation(bodyProg_, "uView");
    uProj_      = glGetUniformLocation(bodyProg_, "uProj");
    uNormalMat_ = glGetUniformLocation(bodyProg_, "uNormalMat");
    uColor_     = glGetUniformLocation(bodyProg_, "uColor");
    uLightPos_  = glGetUniformLocation(bodyProg_, "uLightPos");

    lineProg_ = compile(kLineVert, kLineFrag);
    lView_  = glGetUniformLocation(lineProg_, "uView");
    lProj_  = glGetUniformLocation(lineProg_, "uProj");
    lColor_ = glGetUniformLocation(lineProg_, "uColor");

    buildSphere(20, 28);

    glGenVertexArrays(1, &lineVAO_);
    glGenBuffers(1, &lineVBO_);
    glBindVertexArray(lineVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    // 2D overlay program (labels + dividers): attribute 0 = vec2 pixel position.
    textProg_ = compile(kTextVert, kTextFrag);
    tProj_  = glGetUniformLocation(textProg_, "uProj");
    tColor_ = glGetUniformLocation(textProg_, "uColor");
    glGenVertexArrays(1, &textVAO_);
    glGenBuffers(1, &textVBO_);
    glGenBuffers(1, &textEBO_);
    glBindVertexArray(textVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
    return true;
}

void Renderer::drawScreenLines(int screenW, int screenH,
                               const glm::vec3& color, const std::vector<float>& xy) {
    if (xy.size() < 4) return;
    glm::mat4 P = glm::ortho(0.0f, (float)screenW, (float)screenH, 0.0f);
    glUseProgram(textProg_);
    glUniformMatrix4fv(tProj_, 1, GL_FALSE, glm::value_ptr(P));
    glUniform3fv(tColor_, 1, glm::value_ptr(color));
    glBindVertexArray(textVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO_);
    if (xy.size() > textVBOCap_) {
        glBufferData(GL_ARRAY_BUFFER, xy.size() * sizeof(float), xy.data(), GL_DYNAMIC_DRAW);
        textVBOCap_ = xy.size();
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, xy.size() * sizeof(float), xy.data());
    }
    glDrawArrays(GL_LINES, 0, (GLsizei)(xy.size() / 2));
    glBindVertexArray(0);
}

void Renderer::drawText2D(int screenW, int screenH, float x, float y, float scale,
                          const glm::vec3& color, const char* text) {
    static char quads[60000];
    int numQuads = stb_easy_font_print(0.0f, 0.0f, const_cast<char*>(text),
                                       nullptr, quads, sizeof(quads));
    if (numQuads <= 0) return;
    int numVerts = numQuads * 4;

    std::vector<float> pos; pos.reserve(numVerts * 2);
    for (int i = 0; i < numVerts; ++i) {
        const float* fp = reinterpret_cast<const float*>(quads + i * 16);
        pos.push_back(x + fp[0] * scale);
        pos.push_back(y + fp[1] * scale);
    }
    std::vector<unsigned int> idx; idx.reserve(numQuads * 6);
    for (int q = 0; q < numQuads; ++q) {
        unsigned int b = q * 4;
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }

    glm::mat4 P = glm::ortho(0.0f, (float)screenW, (float)screenH, 0.0f);
    glUseProgram(textProg_);
    glUniformMatrix4fv(tProj_, 1, GL_FALSE, glm::value_ptr(P));
    glUniform3fv(tColor_, 1, glm::value_ptr(color));
    glBindVertexArray(textVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO_);
    if (pos.size() > textVBOCap_) {
        glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(float), pos.data(), GL_DYNAMIC_DRAW);
        textVBOCap_ = pos.size();
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, pos.size() * sizeof(float), pos.data());
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, textEBO_);
    if (idx.size() > textEBOCap_) {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int),
                     idx.data(), GL_DYNAMIC_DRAW);
        textEBOCap_ = idx.size();
    } else {
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx.size() * sizeof(unsigned int), idx.data());
    }
    glDrawElements(GL_TRIANGLES, (GLsizei)idx.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Renderer::drawBodies(const glm::mat4& view, const glm::mat4& proj,
                          const std::vector<Body>& bodies, const glm::vec3& lightPos) {
    glUseProgram(bodyProg_);
    glUniformMatrix4fv(uView_, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(uProj_, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform3fv(uLightPos_, 1, glm::value_ptr(lightPos));
    glBindVertexArray(sphereVAO_);
    for (const auto& b : bodies) {
        float rad = b.visualRadius();
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(b.pos));
        model = glm::scale(model, glm::vec3(rad));
        glm::mat3 nrm = glm::mat3(1.0f); // uniform scale -> normals unaffected
        glUniformMatrix4fv(uModel_, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix3fv(uNormalMat_, 1, GL_FALSE, glm::value_ptr(nrm));
        glUniform3fv(uColor_, 1, glm::value_ptr(b.color));
        glDrawElements(GL_TRIANGLES, sphereIndexCount_, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
}

void Renderer::drawTrail(const glm::mat4& view, const glm::mat4& proj,
                         const glm::vec3& color, const std::vector<float>& verts) {
    if (verts.size() < 8) return; // need at least 2 points
    glUseProgram(lineProg_);
    glUniformMatrix4fv(lView_, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(lProj_, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform3fv(lColor_, 1, glm::value_ptr(color));
    glBindVertexArray(lineVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
    if (verts.size() > lineVBOCapacity_) {
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
        lineVBOCapacity_ = verts.size();
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
    }
    glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)(verts.size() / 4));
    glBindVertexArray(0);
}

void Renderer::shutdown() {
    glDeleteBuffers(1, &sphereVBO_); glDeleteBuffers(1, &sphereEBO_);
    glDeleteVertexArrays(1, &sphereVAO_);
    glDeleteBuffers(1, &lineVBO_); glDeleteVertexArrays(1, &lineVAO_);
    glDeleteBuffers(1, &textVBO_); glDeleteBuffers(1, &textEBO_);
    glDeleteVertexArrays(1, &textVAO_);
    glDeleteProgram(bodyProg_); glDeleteProgram(lineProg_); glDeleteProgram(textProg_);
}
