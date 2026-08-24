#include "GpuBarnesHut.h"
#include "BarnesHut.h"
#include <glad/gl.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Identical traversal to bh::Flat — each invocation walks the flattened tree with skip
// pointers. Tree ints (next/child/first/count) are bit-packed into the float SSBO and
// recovered with floatBitsToInt. 3 vec4 per node.
const char* kCompute = R"GLSL(
#version 430
layout(local_size_x = 128) in;
layout(std430, binding = 0) readonly  buffer Nodes   { vec4 nodes[]; };
layout(std430, binding = 1) readonly  buffer Order   { int  order[]; };
layout(std430, binding = 2) readonly  buffer PosMass { vec4 pm[]; };
layout(std430, binding = 3) writeonly buffer AccBuf  { vec4 accel[]; };
uniform int   uN;
uniform int   uNodes;
uniform float uG;
uniform float uEps2;
uniform float uTheta2;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uN)) return;
    vec3 pi = pm[i].xyz;
    vec3 a = vec3(0.0);
    int idx = 0;
    while (idx < uNodes) {
        vec4 v0 = nodes[idx*3];
        vec4 v1 = nodes[idx*3 + 1];
        vec3 com = v0.xyz; float mass = v0.w;
        float size = v1.x;
        int next  = floatBitsToInt(v1.y);
        int child = floatBitsToInt(v1.z);
        int first = floatBitsToInt(v1.w);
        vec3 d = com - pi; float d2 = dot(d, d);
        if (size*size < uTheta2 * d2) {
            float inv = inversesqrt(d2 + uEps2);
            a += (mass * inv * inv * inv) * d;
            idx = next;
        } else if (child < 0) {
            int count = floatBitsToInt(nodes[idx*3 + 2].x);
            for (int k = 0; k < count; ++k) {
                int j = order[first + k];
                if (j != int(i)) {
                    vec3 dd = pm[j].xyz - pi;
                    float r2 = dot(dd, dd) + uEps2;
                    float inv = inversesqrt(r2);
                    a += (pm[j].w * inv * inv * inv) * dd;
                }
            }
            idx = next;
        } else {
            idx = child;
        }
    }
    accel[i] = vec4(uG * a, 0.0);
}
)GLSL";

GLuint g_prog = 0, g_nodeBuf = 0, g_orderBuf = 0, g_pmBuf = 0, g_accBuf = 0;
GLint  g_uN = -1, g_uNodes = -1, g_uG = -1, g_uEps2 = -1, g_uTheta2 = -1;
int    g_nodeCap = 0, g_orderCap = 0, g_bodyCap = 0;
bool   g_ok = false;
std::string g_renderer;
std::vector<float> g_nodeData, g_up, g_down;
std::vector<GLint> g_orderData;

inline float asFloat(int v) { float f; std::memcpy(&f, &v, 4); return f; }

bool compileProgram() {
    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(sh, 1, &kCompute, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; GLsizei n = 0; glGetShaderInfoLog(sh, sizeof(log), &n, log);
        std::fprintf(stderr, "[bh-gpu] compute compile failed:\n%.*s\n", (int)n, log);
        glDeleteShader(sh); return false; }
    g_prog = glCreateProgram();
    glAttachShader(g_prog, sh); glLinkProgram(g_prog); glDeleteShader(sh);
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; GLsizei n = 0; glGetProgramInfoLog(g_prog, sizeof(log), &n, log);
        std::fprintf(stderr, "[bh-gpu] link failed:\n%.*s\n", (int)n, log);
        glDeleteProgram(g_prog); g_prog = 0; return false; }
    g_uN      = glGetUniformLocation(g_prog, "uN");
    g_uNodes  = glGetUniformLocation(g_prog, "uNodes");
    g_uG      = glGetUniformLocation(g_prog, "uG");
    g_uEps2   = glGetUniformLocation(g_prog, "uEps2");
    g_uTheta2 = glGetUniformLocation(g_prog, "uTheta2");
    return true;
}

void growBuf(GLuint& buf, int& cap, int need, int elemBytes, GLenum usage) {
    if (need <= cap) return;
    int c = cap ? cap : 256; while (c < need) c *= 2;
    if (!buf) glGenBuffers(1, &buf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)c * elemBytes, nullptr, usage);
    cap = c;
}

} // namespace

namespace gpubh {

bool init() {
    if (g_ok) return true;
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 4 || (major == 4 && minor < 3)) return false;
    if (const GLubyte* r = glGetString(GL_RENDERER)) g_renderer = (const char*)r;
    if (!compileProgram()) return false;
    g_ok = true;
    return true;
}

bool available() { return g_ok; }
const char* deviceInfo() { return g_renderer.c_str(); }

void accelerations(const std::vector<glm::dvec3>& pos, const std::vector<double>& mass,
                   double G, double softening, double theta, std::vector<glm::dvec3>& accOut) {
    const int n = (int)pos.size();
    accOut.assign(n, glm::dvec3(0.0));
    if (!g_ok || n == 0) return;

    bh::Flat f = bh::buildFlat(pos, mass, 32);

    if ((int)g_nodeData.size() < f.n * 12) g_nodeData.resize(f.n * 12);
    for (int i = 0; i < f.n; ++i) {
        float* b = &g_nodeData[i * 12];
        b[0] = (float)f.com[i].x; b[1] = (float)f.com[i].y; b[2] = (float)f.com[i].z;
        b[3] = (float)f.mass[i];  b[4] = (float)f.size[i];
        b[5] = asFloat(f.next[i]); b[6] = asFloat(f.child[i]);
        b[7] = asFloat(f.first[i]); b[8] = asFloat(f.count[i]);
        b[9] = b[10] = b[11] = 0.0f;
    }
    if ((int)g_orderData.size() < (int)f.order.size()) g_orderData.resize(f.order.size());
    for (size_t i = 0; i < f.order.size(); ++i) g_orderData[i] = (GLint)f.order[i];
    if ((int)g_up.size() < n * 4) { g_up.resize(n * 4); g_down.resize(n * 4); }
    for (int i = 0; i < n; ++i) {
        g_up[4*i+0] = (float)pos[i].x; g_up[4*i+1] = (float)pos[i].y;
        g_up[4*i+2] = (float)pos[i].z; g_up[4*i+3] = (float)mass[i];
    }

    growBuf(g_nodeBuf,  g_nodeCap,  f.n * 12,          sizeof(float), GL_DYNAMIC_DRAW);
    growBuf(g_orderBuf, g_orderCap, (int)f.order.size(), sizeof(GLint), GL_DYNAMIC_DRAW);
    growBuf(g_pmBuf,    g_bodyCap,  n * 4,             sizeof(float), GL_DYNAMIC_DRAW);
    if (!g_accBuf) glGenBuffers(1, &g_accBuf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_accBuf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)g_bodyCap * sizeof(float), nullptr, GL_DYNAMIC_READ);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_nodeBuf);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)f.n * 12 * sizeof(float), g_nodeData.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_orderBuf);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)f.order.size() * sizeof(GLint), g_orderData.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_pmBuf);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)n * 4 * sizeof(float), g_up.data());

    glUseProgram(g_prog);
    glUniform1i(g_uN, n);
    glUniform1i(g_uNodes, f.n);
    glUniform1f(g_uG, (float)G);
    glUniform1f(g_uEps2, (float)(softening * softening));
    glUniform1f(g_uTheta2, (float)(theta * theta));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_nodeBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, g_orderBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, g_pmBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, g_accBuf);

    int groups = (n + 127) / 128;
    glDispatchCompute((GLuint)groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_accBuf);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)n * 4 * sizeof(float), g_down.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    for (int i = 0; i < n; ++i)
        accOut[i] = glm::dvec3(g_down[4*i+0], g_down[4*i+1], g_down[4*i+2]);
}

void shutdown() {
    for (GLuint* b : {&g_nodeBuf, &g_orderBuf, &g_pmBuf, &g_accBuf})
        if (*b) { glDeleteBuffers(1, b); *b = 0; }
    if (g_prog) glDeleteProgram(g_prog);
    g_prog = 0; g_ok = false; g_nodeCap = g_orderCap = g_bodyCap = 0;
}

} // namespace gpubh
