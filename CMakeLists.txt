cmake_minimum_required(VERSION 3.16)
project(Gravity3D LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_OPENXR "Build the real VR path via OpenXR (needs a runtime + headset)" OFF)

if(MSVC)
    add_compile_options(/W3 /permissive-)
    add_definitions(-D_USE_MATH_DEFINES)   # for M_PI on MSVC
endif()

include(FetchContent)

# --- GLFW (windowing + input) ---
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# --- GLM (math, header-only) ---
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1)
FetchContent_MakeAvailable(glm)

# --- GLAD (vendored, generated for gl core 3.3) ---
add_library(glad STATIC src/third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC src/third_party/glad/include)

# --- application ---
add_executable(gravity3d
    src/main.cpp
    src/Simulation.cpp
    src/FileLoader.cpp
    src/Camera.cpp
    src/Renderer.cpp)

target_link_libraries(gravity3d PRIVATE glad glfw glm::glm)
target_include_directories(gravity3d PRIVATE src)

# --- optional OpenXR VR backend ---
if(ENABLE_OPENXR)
    FetchContent_Declare(openxr
        GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
        GIT_TAG        release-1.1.43)
    FetchContent_MakeAvailable(openxr)
    target_sources(gravity3d PRIVATE src/vr/OpenXRBackend.cpp)
    target_link_libraries(gravity3d PRIVATE openxr_loader)
    target_compile_definitions(gravity3d PRIVATE ENABLE_OPENXR)
endif()

if(WIN32)
    target_link_libraries(gravity3d PRIVATE opengl32)
endif()

# Copy the sample scene next to the binary so it can be run from the build dir.
add_custom_command(TARGET gravity3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:gravity3d>/data)
