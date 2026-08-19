#include "GpuNbody.h"
#include <glad/gl.h>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const int TILE = 256;   // workgroup / shared-memory tile size

// Tiled N-body kernel (GPU Gems 3 style): each invocation accumulates the
// acceleration on one body by streaming all bodies through shared memory in
// TILE-sized blocks. The self term (j==i) has d=0 so it contributes nothing, and
// padding lanes (j>=N) carry mass 0, so no branching is needed in the inner loop.
const char* kCompute = R"GLSL(
#version 430
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly  buffer PosMass { vec4 posMass[]; };
layout(std430, binding = 1) writeonly buffer AccBuf  { vec4 accel[]; };
uniform int   uN;
uniform float uG;
uniform float uEps2;
shared vec4 tile[256];
void main() {
    uint i  = gl_GlobalInvocationID.x;
    uint lx = gl_LocalInvocationID.x;
    vec3 pi = (i < uint(uN)) ? posMass[i].xyz : vec3(0.0);
    vec3 ai = vec3(0.0);
    uint tiles = (uint(uN) + 255u) / 256u;
    for (uint t = 0u; t < tiles; ++t) {
        uint j = t * 256u + lx;
        tile[lx] = (j < uint(uN)) ? posMass[j] : vec4(0.0);
        barrier();
        for (uint k = 0u; k < 256u; ++k) {
            vec4 pj  = tile[k];
            vec3 d   = pj.xyz - pi;
            float r2 = dot(d, d) + uEps2;
            float inv = inversesqrt(r2);
            ai += (pj.w * inv * inv * inv) * d;   // j==i -> d=0 -> 0; padding w=0 -> 0
        }
        barrier();
    }
    if (i < uint(uN)) accel[i] = vec4(uG * ai, 0.0);
}
)GLSL";

GLuint g_prog = 0, g_posBuf = 0, g_accBuf = 0;
GLint  g_uN = -1, g_uG = -1, g_uEps2 = -1;
int    g_cap = 0;            // current buffer capacity (bodies)
bool   g_ok = false;
std::string g_renderer;
std::vector<float> g_up;     // scratch upload (vec4 per body)
std::vector<float> g_down;   // scratch readback (vec4 per body)

bool compileProgram() {
    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(sh, 1, &kCompute, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]; GLsizei n = 0; glGetShaderInfoLog(sh, sizeof(log), &n, log);
        std::fprintf(stderr, "[gpu] compute shader compile failed:\n%.*s\n", (int)n, log);
        glDeleteShader(sh); return false;
    }
    g_prog = glCreateProgram();
    glAttachShader(g_prog, sh);
    glLinkProgram(g_prog);
    glDeleteShader(sh);
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048]; GLsizei n = 0; glGetProgramInfoLog(g_prog, sizeof(log), &n, log);
        std::fprintf(stderr, "[gpu] program link failed:\n%.*s\n", (int)n, log);
        glDeleteProgram(g_prog); g_prog = 0; return false;
    }
    g_uN    = glGetUniformLocation(g_prog, "uN");
    g_uG    = glGetUniformLocation(g_prog, "uG");
    g_uEps2 = glGetUniformLocation(g_prog, "uEps2");
    return true;
}

void ensureCapacity(int n) {
    if (n <= g_cap) return;
    int cap = g_cap ? g_cap : TILE;
    while (cap < n) cap *= 2;
    if (!g_posBuf) glGenBuffers(1, &g_posBuf);
    if (!g_accBuf) glGenBuffers(1, &g_accBuf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_posBuf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)cap * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_accBuf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)cap * 4 * sizeof(float), nullptr, GL_DYNAMIC_READ);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    g_cap = cap;
    g_up.resize((size_t)cap * 4);
    g_down.resize((size_t)cap * 4);
}

} // namespace

namespace gpu {

bool init() {
    if (g_ok) return true;
    // Need GL 4.3 for compute shaders + SSBOs.
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 4 || (major == 4 && minor < 3)) {
        std::fprintf(stderr, "[gpu] OpenGL %d.%d < 4.3; GPU solver unavailable.\n", major, minor);
        return false;
    }
    if (const GLubyte* r = glGetString(GL_RENDERER)) g_renderer = (const char*)r;
    if (!compileProgram()) return false;
    g_ok = true;
    return true;
}

bool available() { return g_ok; }
const char* deviceInfo() { return g_renderer.c_str(); }

void accelerations(const std::vector<glm::dvec3>& pos, const std::vector<double>& mass,
                   double G, double softening, std::vector<glm::dvec3>& accOut) {
    const int n = (int)pos.size();
    accOut.assign(n, glm::dvec3(0.0));
    if (!g_ok || n == 0) return;
    ensureCapacity(n);

    // pack positions+mass as vec4 float, zero the padding lanes (mass 0)
    for (int i = 0; i < n; ++i) {
        g_up[4*i+0] = (float)pos[i].x; g_up[4*i+1] = (float)pos[i].y;
        g_up[4*i+2] = (float)pos[i].z; g_up[4*i+3] = (float)mass[i];
    }
    int groups = (n + TILE - 1) / TILE;
    int padded = groups * TILE;
    for (int i = n; i < padded; ++i) { g_up[4*i+0]=g_up[4*i+1]=g_up[4*i+2]=g_up[4*i+3]=0.0f; }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_posBuf);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)padded * 4 * sizeof(float), g_up.data());

    glUseProgram(g_prog);
    glUniform1i(g_uN, n);
    glUniform1f(g_uG, (float)G);
    glUniform1f(g_uEps2, (float)(softening * softening));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_posBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, g_accBuf);

    glDispatchCompute((GLuint)groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_accBuf);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)n * 4 * sizeof(float), g_down.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    for (int i = 0; i < n; ++i)
        accOut[i] = glm::dvec3(g_down[4*i+0], g_down[4*i+1], g_down[4*i+2]);
}

void shutdown() {
    if (g_posBuf) glDeleteBuffers(1, &g_posBuf);
    if (g_accBuf) glDeleteBuffers(1, &g_accBuf);
    if (g_prog)   glDeleteProgram(g_prog);
    g_posBuf = g_accBuf = 0; g_prog = 0; g_ok = false; g_cap = 0;
}

} // namespace gpu
