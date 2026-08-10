// Viewer-only OpenXR + OpenGL backend. Compiled only when ENABLE_OPENXR=ON.
#include "OpenXRBackend.h"

#include <glad/gl.h>

#ifdef _WIN32
  #define XR_USE_PLATFORM_WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif
#define XR_USE_GRAPHICS_API_OPENGL

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
bool xrOk(XrResult r, const char* what) {
    if (XR_SUCCEEDED(r)) return true;
    std::fprintf(stderr, "[OpenXR] %s failed (%d)\n", what, (int)r);
    return false;
}

// Build a projection matrix from an OpenXR asymmetric FOV (angles in radians).
glm::mat4 projFromFov(const XrFovf& fov, float nearZ, float farZ) {
    float l = std::tan(fov.angleLeft),  r = std::tan(fov.angleRight);
    float d = std::tan(fov.angleDown),  u = std::tan(fov.angleUp);
    float w = r - l, h = u - d;
    glm::mat4 m(0.0f);
    m[0][0] = 2.0f / w;
    m[1][1] = 2.0f / h;
    m[2][0] = (r + l) / w;
    m[2][1] = (u + d) / h;
    m[2][2] = -(farZ + nearZ) / (farZ - nearZ);
    m[2][3] = -1.0f;
    m[3][2] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    return m;
}

glm::mat4 viewFromPose(const XrPosef& p) {
    glm::quat q(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z);
    glm::vec3 t(p.position.x, p.position.y, p.position.z);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(q);
    return glm::inverse(model);
}
} // namespace

struct OpenXRBackend::Impl {
    XrInstance   instance   = XR_NULL_HANDLE;
    XrSystemId   systemId   = XR_NULL_SYSTEM_ID;
    XrSession    session    = XR_NULL_HANDLE;
    XrSpace      appSpace   = XR_NULL_HANDLE;
    XrSessionState state    = XR_SESSION_STATE_UNKNOWN;
    bool running            = false;

    std::vector<XrViewConfigurationView> viewConfigs;
    std::vector<XrSwapchain>             swapchains;
    std::vector<std::vector<XrSwapchainImageOpenGLKHR>> images;
    int32_t width = 0, height = 0;

    GLuint fbo = 0, depth = 0;
};

bool OpenXRBackend::init() {
    impl_ = new Impl();
    Impl& d = *impl_;

    // --- Instance with the OpenGL extension ---
    const char* exts[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
    XrInstanceCreateInfo ici{ XR_TYPE_INSTANCE_CREATE_INFO };
    std::strcpy(ici.applicationInfo.applicationName, "Gravity3D");
    ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = exts;
    if (!xrOk(xrCreateInstance(&ici, &d.instance), "xrCreateInstance")) return false;

    XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!xrOk(xrGetSystem(d.instance, &sgi, &d.systemId), "xrGetSystem")) return false;

    // --- Graphics requirements (must be queried before session) ---
    PFN_xrGetOpenGLGraphicsRequirementsKHR pfnReq = nullptr;
    xrGetInstanceProcAddr(d.instance, "xrGetOpenGLGraphicsRequirementsKHR",
                          (PFN_xrVoidFunction*)&pfnReq);
    XrGraphicsRequirementsOpenGLKHR gr{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    if (pfnReq) pfnReq(d.instance, d.systemId, &gr);

#ifdef _WIN32
    XrGraphicsBindingOpenGLWin32KHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    binding.hDC   = wglGetCurrentDC();
    binding.hGLRC = wglGetCurrentContext();
#else
    std::fprintf(stderr, "[OpenXR] Only the Win32 GL binding is wired up here.\n");
    return false;
#endif

    XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
    sci.systemId = d.systemId;
#ifdef _WIN32
    sci.next     = &binding;
#endif
    if (!xrOk(xrCreateSession(d.instance, &sci, &d.session), "xrCreateSession")) return false;

    // Reference space (STAGE if available, else LOCAL).
    XrReferenceSpaceCreateInfo rsci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rsci.referenceSpaceType   = XR_REFERENCE_SPACE_TYPE_STAGE;
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    if (xrCreateReferenceSpace(d.session, &rsci, &d.appSpace) != XR_SUCCESS) {
        rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        xrCreateReferenceSpace(d.session, &rsci, &d.appSpace);
    }

    // View configuration (stereo) and per-view swapchains.
    uint32_t viewCount = 0;
    xrEnumerateViewConfigurationViews(d.instance, d.systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
    d.viewConfigs.resize(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    xrEnumerateViewConfigurationViews(d.instance, d.systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, d.viewConfigs.data());

    d.width  = d.viewConfigs[0].recommendedImageRectWidth;
    d.height = d.viewConfigs[0].recommendedImageRectHeight;

    d.swapchains.resize(viewCount);
    d.images.resize(viewCount);
    for (uint32_t i = 0; i < viewCount; ++i) {
        XrSwapchainCreateInfo sc{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        sc.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sc.format      = GL_SRGB8_ALPHA8;
        sc.width       = d.width;
        sc.height      = d.height;
        sc.sampleCount = 1;
        sc.faceCount   = 1;
        sc.arraySize   = 1;
        sc.mipCount    = 1;
        if (!xrOk(xrCreateSwapchain(d.session, &sc, &d.swapchains[i]), "xrCreateSwapchain"))
            return false;
        uint32_t imgCount = 0;
        xrEnumerateSwapchainImages(d.swapchains[i], 0, &imgCount, nullptr);
        d.images[i].resize(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
        xrEnumerateSwapchainImages(d.swapchains[i], imgCount, &imgCount,
            (XrSwapchainImageBaseHeader*)d.images[i].data());
    }

    glGenFramebuffers(1, &d.fbo);
    glGenRenderbuffers(1, &d.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, d.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, d.width, d.height);

    ready_ = true;
    std::printf("[OpenXR] Initialised (%u views, %dx%d per eye).\n",
                viewCount, d.width, d.height);
    return true;
}

bool OpenXRBackend::renderFrame(const RenderView& cb) {
    Impl& d = *impl_;

    // Pump session-state events.
    XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(d.instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* s = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            d.state = s->state;
            if (d.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                xrBeginSession(d.session, &bi);
                d.running = true;
            } else if (d.state == XR_SESSION_STATE_STOPPING) {
                xrEndSession(d.session);
                d.running = false;
            } else if (d.state == XR_SESSION_STATE_EXITING ||
                       d.state == XR_SESSION_STATE_LOSS_PENDING) {
                return false;
            }
        }
        ev = { XR_TYPE_EVENT_DATA_BUFFER };
    }
    if (!d.running) return true; // idle until the runtime is ready

    XrFrameState fs{ XR_TYPE_FRAME_STATE };
    XrFrameWaitInfo fwi{ XR_TYPE_FRAME_WAIT_INFO };
    xrWaitFrame(d.session, &fwi, &fs);
    XrFrameBeginInfo fbi{ XR_TYPE_FRAME_BEGIN_INFO };
    xrBeginFrame(d.session, &fbi);

    std::vector<XrCompositionLayerProjectionView> projViews;
    XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };

    if (fs.shouldRender) {
        uint32_t viewCount = (uint32_t)d.swapchains.size();
        std::vector<XrView> views(viewCount, { XR_TYPE_VIEW });
        XrViewState vs{ XR_TYPE_VIEW_STATE };
        XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vli.displayTime           = fs.predictedDisplayTime;
        vli.space                 = d.appSpace;
        uint32_t got = 0;
        xrLocateViews(d.session, &vli, &vs, viewCount, &got, views.data());

        projViews.resize(viewCount);
        for (uint32_t i = 0; i < viewCount; ++i) {
            uint32_t imgIndex = 0;
            XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            xrAcquireSwapchainImage(d.swapchains[i], &ai, &imgIndex);
            XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            wi.timeout = XR_INFINITE_DURATION;
            xrWaitSwapchainImage(d.swapchains[i], &wi);

            GLuint colorTex = d.images[i][imgIndex].image;
            glBindFramebuffer(GL_FRAMEBUFFER, d.fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, d.depth);
            glViewport(0, 0, d.width, d.height);
            glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glm::mat4 view = viewFromPose(views[i].pose);
            glm::mat4 proj = projFromFov(views[i].fov, 0.05f, 8000.0f);
            cb(view, proj, (int)i);

            XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            xrReleaseSwapchainImage(d.swapchains[i], &ri);

            projViews[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            projViews[i].pose = views[i].pose;
            projViews[i].fov  = views[i].fov;
            projViews[i].subImage.swapchain = d.swapchains[i];
            projViews[i].subImage.imageRect.offset = { 0, 0 };
            projViews[i].subImage.imageRect.extent = { d.width, d.height };
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        layer.space     = d.appSpace;
        layer.viewCount = (uint32_t)projViews.size();
        layer.views     = projViews.data();
    }

    XrCompositionLayerBaseHeader* layers[] = { (XrCompositionLayerBaseHeader*)&layer };
    XrFrameEndInfo fei{ XR_TYPE_FRAME_END_INFO };
    fei.displayTime          = fs.predictedDisplayTime;
    fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    fei.layerCount           = fs.shouldRender ? 1 : 0;
    fei.layers               = fs.shouldRender ? layers : nullptr;
    xrEndFrame(d.session, &fei);
    return true;
}

void OpenXRBackend::shutdown() {
    if (!impl_) return;
    Impl& d = *impl_;
    if (d.fbo)   glDeleteFramebuffers(1, &d.fbo);
    if (d.depth) glDeleteRenderbuffers(1, &d.depth);
    for (auto s : d.swapchains) if (s) xrDestroySwapchain(s);
    if (d.appSpace) xrDestroySpace(d.appSpace);
    if (d.session)  xrDestroySession(d.session);
    if (d.instance) xrDestroyInstance(d.instance);
    delete impl_; impl_ = nullptr; ready_ = false;
}
