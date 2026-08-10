#pragma once
#include <string>
#include <vector>
#include "Body.h"

struct SceneConfig {
    double G            = 1.0;
    double softening    = 0.05;
    double dt           = 0.004;
    double timeScale    = 1.0;
    bool   zeroMomentum = true;
};

// Parse a scene file into bodies + config. Returns false on fatal error (message in 'err').
bool loadScene(const std::string& path,
               std::vector<Body>& out,
               SceneConfig& cfg,
               std::string& err);
