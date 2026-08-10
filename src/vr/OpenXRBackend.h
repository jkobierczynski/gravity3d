#pragma once
// Optional VR backend (compiled only when ENABLE_OPENXR=ON).
// Viewer-only: no controller input, just stereo head-tracked rendering.
//
// NOTE: This path targets a real PC-VR headset via an OpenXR runtime
// (SteamVR, Oculus/Meta, WMR, Monado...). It compiles against the Khronos
// OpenXR-SDK but has not been exercised on hardware in this project's CI,
// so treat it as a solid starting point that may need small tweaks per runtime.

#include <functional>
#include <glm/glm.hpp>

class OpenXRBackend {
public:
    // Called once per eye/view each frame. Render your scene with these matrices.
    using RenderView = std::function<void(const glm::mat4& view,
                                          const glm::mat4& proj,
                                          int viewIndex)>;

    bool init();                       // create instance/session/swapchains
    bool isReady() const { return ready_; }

    // Run one frame: wait, locate views, render each view via cb, submit.
    // Returns false when the runtime asks us to stop.
    bool renderFrame(const RenderView& cb);

    void shutdown();

private:
    bool ready_ = false;
    struct Impl;                       // hide OpenXR types from this header
    Impl* impl_ = nullptr;
};
