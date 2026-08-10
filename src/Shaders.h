#pragma once

// Sphere (body) shaders: simple Lambert + ambient + slight emissive, headlight at camera.
static const char* kBodyVert = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;
out vec3 vNormal;
out vec3 vWorldPos;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos  = world.xyz;
    vNormal    = normalize(uNormalMat * aNormal);
    gl_Position = uProj * uView * world;
}
)GLSL";

static const char* kBodyFrag = R"GLSL(
#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
uniform vec3 uColor;
uniform vec3 uLightPos;
out vec4 FragColor;
void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightPos - vWorldPos);
    float diff = max(dot(N, L), 0.0);
    vec3 col = 0.22 * uColor + 0.16 * uColor + diff * uColor * 0.9;
    FragColor = vec4(col, 1.0);
}
)GLSL";

// Line/trail shaders: per-vertex alpha for fading trails.
static const char* kLineVert = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aAlpha;
uniform mat4 uView;
uniform mat4 uProj;
out float vAlpha;
void main() {
    vAlpha = aAlpha;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)GLSL";

static const char* kLineFrag = R"GLSL(
#version 330 core
in float vAlpha;
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(uColor, vAlpha);
}
)GLSL";

// 2D text/overlay shaders: positions are pixel coordinates, mapped by an ortho matrix.
static const char* kTextVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
uniform mat4 uProj;
void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* kTextFrag = R"GLSL(
#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)GLSL";
