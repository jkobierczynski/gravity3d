// Gravity3D — a 3D N-body gravity visualiser.
//   Projections: Front / Isometric / Axonometric / Oblique
//   Multiview:   engineering 3-view, third-angle (American) or first-angle (ISO/EU)
//   Stereo:      Anaglyph (red/cyan), Side-by-side parallel & cross-eye, VR (SBS), OpenXR
//   Controls:    speed, pause, restart, trail modes.  See printHelp() below.
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "Body.h"
#include "Simulation.h"
#include "FileLoader.h"
#include "Camera.h"
#include "Renderer.h"
#include "Parallel.h"
#include "GpuNbody.h"

#ifdef ENABLE_OPENXR
  #include "vr/OpenXRBackend.h"
#endif

enum class RenderMode { Mono, Anaglyph, StereoParallel, StereoCross, Multiview, VR_SBS, VR_OpenXR };
enum class TrailMode  { Off, Follow, Stay };

// Canonical orthographic views used by the engineering multiview layout.
enum class StdView { Front, Top, Right, Iso };

struct AppState {
    Camera*       cam       = nullptr;
    Simulation*   sim       = nullptr;
    std::vector<std::vector<glm::vec3>>* trails = nullptr;
    RenderMode    mode      = RenderMode::Mono;
    TrailMode     trailMode = TrailMode::Follow;
    bool          thirdAngle = true;   // multiview layout: true = American (3rd), false = European (1st)
    bool          dragging  = false;
    double        lastX = 0, lastY = 0;
    bool          fullscreen = false;
    int           savedX = 100, savedY = 100, savedW = 1280, savedH = 720;
    // VR "hologram" placement (STAGE space, metres): scale sim units -> metres,
    // and position the model in front of / above the floor origin.
    float         vrScale = 0.03f;
    float         vrHeight = 1.4f;    // metres above the floor
    float         vrForward = -1.6f;  // metres along stage-forward (-Z)
};

static const char* projName(ProjectionType t) {
    switch (t) {
        case ProjectionType::Front:       return "Front";
        case ProjectionType::Isometric:   return "Isometric";
        case ProjectionType::Axonometric: return "Axonometric";
        case ProjectionType::Oblique:     return "Oblique";
    }
    return "?";
}
static const char* modeName(RenderMode m) {
    switch (m) {
        case RenderMode::Mono:           return "Mono";
        case RenderMode::Anaglyph:       return "Anaglyph(R/C)";
        case RenderMode::StereoParallel: return "Stereo-Parallel";
        case RenderMode::StereoCross:    return "Stereo-Cross";
        case RenderMode::Multiview:      return "Multiview";
        case RenderMode::VR_SBS:         return "VR-SBS";
        case RenderMode::VR_OpenXR:      return "VR-OpenXR";
    }
    return "?";
}
static const char* trailName(TrailMode t) {
    return t == TrailMode::Off ? "Off" : t == TrailMode::Follow ? "Follow" : "Stay";
}

static void printHelp() {
    std::printf(
    "\n=== Gravity3D controls ===\n"
    "  Projection:   1 Front   2 Isometric   3 Axonometric   4 Oblique\n"
    "  View mode:    F1 Mono   F2 Anaglyph   F3 Stereo-Parallel   F4 Stereo-Cross\n"
    "                F7 Multiview (engineering 3-view)\n"
    "                F5 VR side-by-side (phone/cardboard, go fullscreen)   F6 VR OpenXR\n"
    "  Multiview:    5  toggle Third-angle (American) / First-angle (ISO/European)\n"
    "  Perspective:  P  (toggle ortho/perspective for Front/Iso/Axono)\n"
    "  Trails:       T  cycle Off / Follow / Stay\n"
    "  Speed:        + / -   (faster / slower)\n"
    "  Pause:        Space        Restart: R\n"
    "  Solver:       G  toggle Direct <-> FMM     ; ' lower/raise FMM order\n"
    "                U  toggle GPU solver (OpenGL compute, if available)\n"
    "  Stereo tune:  [ ] eye separation     , . convergence distance\n"
    "  VR placement: scroll = scale   arrows = move (up/down/near/far)   drag = rotate   0 = reset\n"
    "  Camera:       drag = orbit    scroll = zoom\n"
    "  Fullscreen:   F11        Quit: Esc\n"
    "==========================\n\n");
}

// ---- input callbacks ----
static void keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    auto* s = static_cast<AppState*>(glfwGetWindowUserPointer(w));
    Camera& cam = *s->cam; Simulation& sim = *s->sim;

    auto clearTrails = [&]{ for (auto& t : *s->trails) t.clear(); };

    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_1: cam.setPreset(ProjectionType::Front);       break;
        case GLFW_KEY_2: cam.setPreset(ProjectionType::Isometric);   break;
        case GLFW_KEY_3: cam.setPreset(ProjectionType::Axonometric); break;
        case GLFW_KEY_4: cam.setPreset(ProjectionType::Oblique);     break;
        case GLFW_KEY_F1: s->mode = RenderMode::Mono;           break;
        case GLFW_KEY_F2: s->mode = RenderMode::Anaglyph;       break;
        case GLFW_KEY_F3: s->mode = RenderMode::StereoParallel; break;
        case GLFW_KEY_F4: s->mode = RenderMode::StereoCross;    break;
        case GLFW_KEY_F5: s->mode = RenderMode::VR_SBS;         break;
        case GLFW_KEY_F6: s->mode = RenderMode::VR_OpenXR;      break;
        case GLFW_KEY_F7: s->mode = RenderMode::Multiview;      break;
        case GLFW_KEY_5:  s->thirdAngle = !s->thirdAngle;       break;
        case GLFW_KEY_P:
            if (cam.type != ProjectionType::Oblique) cam.perspective = !cam.perspective;
            break;
        case GLFW_KEY_T:
            s->trailMode = static_cast<TrailMode>((static_cast<int>(s->trailMode) + 1) % 3);
            if (s->trailMode == TrailMode::Off) clearTrails();
            break;
        case GLFW_KEY_EQUAL:
        case GLFW_KEY_KP_ADD:
            sim.timeScale = std::min(200.0, sim.timeScale * 1.25); break;
        case GLFW_KEY_MINUS:
        case GLFW_KEY_KP_SUBTRACT:
            sim.timeScale = std::max(0.01, sim.timeScale / 1.25); break;
        case GLFW_KEY_SPACE: sim.paused = !sim.paused; break;
        case GLFW_KEY_R:     sim.reset(); clearTrails(); break;
        // Gravity solver: Direct (default) <-> FMM, and FMM order.
        case GLFW_KEY_G:          sim.useFMM   = !sim.useFMM;                     break;
        case GLFW_KEY_U:
            if (gpu::available()) sim.useGPU = !sim.useGPU;
            else std::printf("GPU solver unavailable on this system.\n");
            break;
        case GLFW_KEY_SEMICOLON:  sim.fmmOrder = std::max(1,  sim.fmmOrder - 1);  break;
        case GLFW_KEY_APOSTROPHE: sim.fmmOrder = std::min(12, sim.fmmOrder + 1);  break;
        case GLFW_KEY_LEFT_BRACKET:  cam.eyeSep = std::max(0.01, cam.eyeSep * 0.9); break;
        case GLFW_KEY_RIGHT_BRACKET: cam.eyeSep = std::min(20.0, cam.eyeSep * 1.1); break;
        case GLFW_KEY_COMMA:  cam.convergence = std::max(1.0,    cam.convergence * 0.9); break;
        case GLFW_KEY_PERIOD: cam.convergence = std::min(8000.0, cam.convergence * 1.1); break;
        // VR hologram placement (only affects F6): raise/lower and near/far.
        case GLFW_KEY_UP:    s->vrHeight  += 0.1f; break;
        case GLFW_KEY_DOWN:  s->vrHeight  -= 0.1f; break;
        case GLFW_KEY_LEFT:  s->vrForward += 0.1f; break;   // toward you
        case GLFW_KEY_RIGHT: s->vrForward -= 0.1f; break;   // away
        case GLFW_KEY_0:
            s->vrHeight = 1.4f; s->vrForward = -1.6f;
            { double mr = 1.0; for (auto& b : sim.bodies()) mr = std::max(mr, glm::length(b.pos));
              s->vrScale = 0.7f / (float)mr; }
            break;
        case GLFW_KEY_F11: {
            s->fullscreen = !s->fullscreen;
            if (s->fullscreen) {
                glfwGetWindowPos(w, &s->savedX, &s->savedY);
                glfwGetWindowSize(w, &s->savedW, &s->savedH);
                GLFWmonitor* mon = glfwGetPrimaryMonitor();
                const GLFWvidmode* vm = glfwGetVideoMode(mon);
                glfwSetWindowMonitor(w, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
            } else {
                glfwSetWindowMonitor(w, nullptr, s->savedX, s->savedY, s->savedW, s->savedH, 0);
            }
            break;
        }
        default: break;
    }
}
static void mouseButtonCallback(GLFWwindow* w, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    auto* s = static_cast<AppState*>(glfwGetWindowUserPointer(w));
    s->dragging = (action == GLFW_PRESS);
    glfwGetCursorPos(w, &s->lastX, &s->lastY);
}
static void cursorCallback(GLFWwindow* w, double x, double y) {
    auto* s = static_cast<AppState*>(glfwGetWindowUserPointer(w));
    if (!s->dragging) return;
    double dx = x - s->lastX, dy = y - s->lastY;
    s->lastX = x; s->lastY = y;
    s->cam->azimuth   += dx * 0.005;
    s->cam->elevation += dy * 0.005;
    s->cam->clampElevation();
    // Free orbit -> drop into free axonometric so presets don't fight the mouse.
    if (s->cam->type == ProjectionType::Front || s->cam->type == ProjectionType::Oblique)
        s->cam->type = ProjectionType::Axonometric;
}
static void scrollCallback(GLFWwindow* w, double, double dy) {
    auto* s = static_cast<AppState*>(glfwGetWindowUserPointer(w));
    if (s->mode == RenderMode::VR_OpenXR) {
        s->vrScale = std::max(1e-4f, std::min(50.0f, s->vrScale * (float)(1.0 + dy * 0.1)));
    } else {
        s->cam->distance = std::max(2.0, std::min(6000.0, s->cam->distance * (1.0 - dy * 0.1)));
    }
}

int main(int argc, char** argv) {
    std::string scenePath = (argc > 1) ? argv[1] : "data/sample_system.csv";

    std::vector<Body> bodies;
    SceneConfig cfg;
    std::string err;
    if (!loadScene(scenePath, bodies, cfg, err)) {
        std::fprintf(stderr, "Scene load failed: %s\n", err.c_str());
        std::fprintf(stderr, "Usage: gravity3d [scene_file.csv]\n");
        return 1;
    }
    std::printf("Loaded %zu bodies from %s\n", bodies.size(), scenePath.c_str());
    std::printf("Solver threads: %u (set GRAVITY3D_THREADS to override)\n",
                parallel::threadCount());

    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Gravity3D", nullptr, nullptr);
    if (!window) { std::fprintf(stderr, "window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        std::fprintf(stderr, "glad load failed\n"); return 1;
    }
    glEnable(GL_DEPTH_TEST);

    Renderer renderer;
    if (!renderer.init()) { std::fprintf(stderr, "renderer init failed\n"); return 1; }

    // Optional GPU (compute-shader) direct solver — vendor-independent, needs GL 4.3.
    if (gpu::init())
        std::printf("GPU solver: available on \"%s\" (press U to toggle)\n", gpu::deviceInfo());
    else
        std::printf("GPU solver: unavailable (needs an OpenGL 4.3 context)\n");

    Simulation sim;
    sim.G = cfg.G; sim.softening = cfg.softening; sim.dt = cfg.dt; sim.timeScale = cfg.timeScale;
    sim.setInitial(bodies, cfg.zeroMomentum);

    // Fit the camera to the scene extent.
    double maxR = 1.0;
    for (auto& b : bodies) maxR = std::max(maxR, glm::length(b.pos));
    Camera cam;
    cam.setPreset(ProjectionType::Isometric);
    cam.distance    = maxR * 3.0 + 8.0;
    cam.convergence = cam.distance;

    std::vector<std::vector<glm::vec3>> trails(sim.bodies().size());

    AppState state;
    state.cam = &cam; state.sim = &sim; state.trails = &trails;
    // Default: shrink the whole system to ~1.4 m across so it reads as a tabletop orrery.
    state.vrScale = 0.7f / (float)maxR;
    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorCallback);
    glfwSetScrollCallback(window, scrollCallback);

#ifdef ENABLE_OPENXR
    OpenXRBackend xr;
    bool xrTried = false;
#endif

    printHelp();

    // Draw the whole scene (bodies + trails) for a single eye.
    auto drawSceneAndTrails = [&](const glm::mat4& view, const glm::mat4& proj) {
        glm::vec3 light = glm::vec3(cam.eye());
        renderer.drawBodies(view, proj, sim.bodies(), light);
        if (state.trailMode == TrailMode::Off) return;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        const auto& bs = sim.bodies();
        for (size_t i = 0; i < bs.size(); ++i) {
            const auto& tr = trails[i];
            if (tr.size() < 2) continue;
            std::vector<float> v; v.reserve(tr.size() * 4);
            size_t n = tr.size();
            for (size_t k = 0; k < n; ++k) {
                float a = 1.0f;
                if (state.trailMode == TrailMode::Follow)
                    a = 0.12f + 0.88f * (float)k / (float)std::max<size_t>(1, n - 1);
                v.push_back(tr[k].x); v.push_back(tr[k].y); v.push_back(tr[k].z); v.push_back(a);
            }
            renderer.drawTrail(view, proj, bs[i].color, v);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    };

    // Orthographic view+proj for one canonical direction (shared scale, so all panels
    // are drawn at the same size — proper for an engineering multiview sheet).
    auto stdViewProj = [&](StdView sv, float aspect, glm::mat4& V, glm::mat4& P) {
        glm::dvec3 tgt = cam.target;
        double d = cam.distance;
        glm::dvec3 eye, up(0, 1, 0);
        switch (sv) {
            case StdView::Front: eye = tgt + glm::dvec3(0, 0, d);  up = glm::dvec3(0, 1, 0);  break;
            case StdView::Top:   eye = tgt + glm::dvec3(0, d, 0);  up = glm::dvec3(0, 0, -1); break;
            case StdView::Right: eye = tgt + glm::dvec3(d, 0, 0);  up = glm::dvec3(0, 1, 0);  break;
            case StdView::Iso: {
                double el = std::atan(1.0 / std::sqrt(2.0)), az = glm::radians(45.0);
                double ce = std::cos(el), se = std::sin(el);
                eye = tgt + d * glm::dvec3(ce * std::sin(az), se, ce * std::cos(az));
                break;
            }
        }
        V = glm::lookAt(glm::vec3(eye), glm::vec3(tgt), glm::vec3(up));
        float s = (float)(d * 0.5);
        P = glm::ortho(-s * aspect, s * aspect, -s, s, (float)cam.nearP, (float)cam.farP);
    };

    // Engineering multiview: four orthographic panels. The ONLY thing that changes
    // between American (third-angle) and European (first-angle) is where the panels
    // sit relative to Front — top-above vs top-below, right-side-right vs right-side-left.
    auto renderMultiview = [&](int fbW, int fbH, bool thirdAngle) {
        int hw = fbW / 2, hh = fbH / 2;
        struct Rect { int x, y, w, h; };
        Rect TL{0, hh, hw, fbH - hh}, TR{hw, hh, fbW - hw, fbH - hh};
        Rect BL{0, 0, hw, hh},        BR{hw, 0, fbW - hw, hh};

        // Panel placement per projection standard.
        StdView tl, tr, bl, br;
        if (thirdAngle) {            // Front bottom-left; Top above it; Right to its right.
            tl = StdView::Top;   tr = StdView::Iso;
            bl = StdView::Front; br = StdView::Right;
        } else {                     // Front top-right; Top below it; Right-side to its left.
            tl = StdView::Right; tr = StdView::Front;
            bl = StdView::Iso;   br = StdView::Top;
        }

        auto panel = [&](Rect r, StdView sv, glm::vec3 tint) {
            glViewport(r.x, r.y, r.w, r.h);
            glEnable(GL_SCISSOR_TEST);
            glScissor(r.x, r.y, r.w, r.h);
            glClearColor(tint.r, tint.g, tint.b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glm::mat4 V, P;
            stdViewProj(sv, (float)r.w / (float)std::max(1, r.h), V, P);
            drawSceneAndTrails(V, P);
            glDisable(GL_SCISSOR_TEST);
        };
        // Distinct dark tints per physical quadrant so all four panels are unmistakable
        // (and so an empty panel still reads as a panel). Tints stay put while the
        // toggle swaps which view sits in each quadrant.
        glm::vec3 tTL(0.04f, 0.05f, 0.11f), tTR(0.11f, 0.05f, 0.05f);
        glm::vec3 tBL(0.04f, 0.10f, 0.06f), tBR(0.10f, 0.08f, 0.03f);
        panel(TL, tl, tTL); panel(TR, tr, tTR); panel(BL, bl, tBL); panel(BR, br, tBR);

        // Overlay: dividers + labels (screen space, no depth).
        glViewport(0, 0, fbW, fbH);
        glDisable(GL_DEPTH_TEST);
        float W = (float)fbW, H = (float)fbH, X = (float)hw, Y = (float)hh;
        std::vector<float> div = {
            X, 0.0f, X, H,        // vertical split
            0.0f, Y, W, Y,        // horizontal split
            1.0f, 1.0f, W-1, 1.0f,   W-1, 1.0f, W-1, H-1,   // outer frame
            W-1, H-1, 1.0f, H-1,     1.0f, H-1, 1.0f, 1.0f
        };
        renderer.drawScreenLines(fbW, fbH, glm::vec3(0.55f, 0.60f, 0.72f), div);

        auto label = [&](StdView sv) -> const char* {
            switch (sv) {
                case StdView::Front: return "FRONT";
                case StdView::Top:   return "TOP";
                case StdView::Right: return "RIGHT";
                case StdView::Iso:   return "ISO";
            }
            return "";
        };
        // Panel top edge in top-left-origin screen coords = fbH - (y + h).
        auto place = [&](Rect r, StdView sv) {
            float sx = (float)r.x + 8.0f;
            float sy = (float)(fbH - (r.y + r.h)) + 8.0f;
            renderer.drawText2D(fbW, fbH, sx, sy, 2.0f, glm::vec3(0.80f, 0.86f, 0.98f), label(sv));
        };
        place(TL, tl); place(TR, tr); place(BL, bl); place(BR, br);

        renderer.drawText2D(fbW, fbH, (float)fbW * 0.5f - 96.0f, 8.0f, 2.0f,
                            glm::vec3(1.0f, 0.88f, 0.45f),
                            thirdAngle ? "THIRD-ANGLE (AMERICAN)" : "FIRST-ANGLE (ISO / EUROPEAN)");
        glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
        glEnable(GL_DEPTH_TEST);
    };

    double last = glfwGetTime();
    double titleTimer = 0.0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        double dt = std::min(0.1, now - last);   // clamp to avoid big jumps after a stall
        last = now;

        sim.step(dt);

        // Sample trails.
        if (!sim.paused) {
            const auto& bs = sim.bodies();
            size_t cap = (state.trailMode == TrailMode::Follow) ? 700
                       : (state.trailMode == TrailMode::Stay)   ? 30000 : 0;
            for (size_t i = 0; i < bs.size(); ++i) {
                if (state.trailMode == TrailMode::Off) continue;
                glm::vec3 p = glm::vec3(bs[i].pos);
                auto& tr = trails[i];
                bool add = tr.empty();
                if (!add) { glm::vec3 d = p - tr.back();
                            add = (d.x*d.x + d.y*d.y + d.z*d.z) >= 0.0009f; }  // ~0.03 unit spacing
                if (add) tr.push_back(p);
                if (tr.size() > cap) tr.erase(tr.begin(), tr.begin() + (tr.size() - cap));
            }
        }

        int fbW, fbH; glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbH == 0) fbH = 1;
        float aspect = (float)fbW / (float)fbH;

#ifdef ENABLE_OPENXR
        if (state.mode == RenderMode::VR_OpenXR) {
            if (!xrTried) { xrTried = true;
                if (!xr.init()) std::fprintf(stderr, "[OpenXR] init failed; is a runtime running?\n"); }
            if (xr.isReady()) {
                // Orbit angles (mouse drag) rotate the whole hologram about its centre.
                glm::mat4 rot = glm::rotate(glm::mat4(1.0f), (float)cam.azimuth,   glm::vec3(0, 1, 0))
                              * glm::rotate(glm::mat4(1.0f), (float)cam.elevation, glm::vec3(1, 0, 0));
                glm::mat4 world = glm::translate(glm::mat4(1.0f),
                                      glm::vec3(0.0f, state.vrHeight, state.vrForward))
                                * rot
                                * glm::scale(glm::mat4(1.0f), glm::vec3(state.vrScale));
                xr.renderFrame([&](const glm::mat4& v, const glm::mat4& p, int) {
                    drawSceneAndTrails(v * world, p);
                });
            }
            // Mirror a mono view to the desktop window.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, fbW, fbH);
            glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawSceneAndTrails(cam.view(), cam.proj(aspect));
            glfwSwapBuffers(window);
            continue;
        }
#endif
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 vL, pL, vR, pR;
        switch (state.mode) {
            case RenderMode::Mono:
                drawSceneAndTrails(cam.view(), cam.proj(aspect));
                break;
            case RenderMode::Anaglyph:
                cam.stereoEye(true,  aspect, vL, pL);
                cam.stereoEye(false, aspect, vR, pR);
                glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
                drawSceneAndTrails(vL, pL);
                glClear(GL_DEPTH_BUFFER_BIT);
                glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE);
                drawSceneAndTrails(vR, pR);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                break;
            case RenderMode::StereoParallel:
            case RenderMode::VR_SBS: {
                int hw = fbW / 2;
                float ha = (float)hw / (float)fbH;
                cam.stereoEye(true,  ha, vL, pL);
                cam.stereoEye(false, ha, vR, pR);
                glViewport(0, 0, hw, fbH);        drawSceneAndTrails(vL, pL);
                glViewport(hw, 0, fbW - hw, fbH); drawSceneAndTrails(vR, pR);
                break;
            }
            case RenderMode::StereoCross: {
                int hw = fbW / 2;
                float ha = (float)hw / (float)fbH;
                cam.stereoEye(true,  ha, vL, pL);
                cam.stereoEye(false, ha, vR, pR);
                glViewport(0, 0, hw, fbH);        drawSceneAndTrails(vR, pR); // swapped
                glViewport(hw, 0, fbW - hw, fbH); drawSceneAndTrails(vL, pL);
                break;
            }
            case RenderMode::Multiview:
                renderMultiview(fbW, fbH, state.thirdAngle);
                break;
            case RenderMode::VR_OpenXR:
                // Only reachable when ENABLE_OPENXR is off: fall back to SBS.
                { int hw = fbW / 2; float ha = (float)hw / (float)fbH;
                  cam.stereoEye(true, ha, vL, pL); cam.stereoEye(false, ha, vR, pR);
                  glViewport(0, 0, hw, fbH);        drawSceneAndTrails(vL, pL);
                  glViewport(hw, 0, fbW - hw, fbH); drawSceneAndTrails(vR, pR); }
                break;
        }

        glfwSwapBuffers(window);

        titleTimer += dt;
        if (titleTimer > 0.25) {
            titleTimer = 0.0;
            char title[320];
            char proj[64];
            char solver[32];
            if      (sim.useGPU && gpu::available()) std::snprintf(solver, sizeof(solver), "GPU");
            else if (sim.useFMM) std::snprintf(solver, sizeof(solver), "FMM p=%d", sim.fmmOrder);
            else            std::snprintf(solver, sizeof(solver), "direct");
            if (state.mode == RenderMode::Multiview)
                std::snprintf(proj, sizeof(proj), "%s",
                    state.thirdAngle ? "3rd-angle(US)" : "1st-angle(EU)");
            else
                std::snprintf(proj, sizeof(proj), "%s%s", projName(cam.type),
                    (cam.perspective && cam.type != ProjectionType::Oblique) ? "(persp)" : "");
            std::snprintf(title, sizeof(title),
                "Gravity3D | %s | proj:%s | trails:%s | speed:%.2fx | %s | %s | t=%.1f",
                modeName(state.mode), proj,
                trailName(state.trailMode), sim.timeScale,
                solver,
                sim.paused ? "PAUSED" : "running", sim.simTime());
            glfwSetWindowTitle(window, title);
        }
    }

#ifdef ENABLE_OPENXR
    xr.shutdown();
#endif
    gpu::shutdown();
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
