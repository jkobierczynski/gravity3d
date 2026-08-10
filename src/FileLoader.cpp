#include "FileLoader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}
bool toBool(const std::string& v) {
    std::string l = lower(trim(v));
    return (l == "1" || l == "true" || l == "yes" || l == "on");
}
}

bool loadScene(const std::string& path, std::vector<Body>& out,
               SceneConfig& cfg, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "Could not open scene file: " + path; return false; }

    out.clear();
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        // @key=value configuration directives
        if (t[0] == '@') {
            auto eq = t.find('=');
            if (eq == std::string::npos) continue;
            std::string key = lower(trim(t.substr(1, eq - 1)));
            std::string val = trim(t.substr(eq + 1));
            try {
                if      (key == "g")            cfg.G            = std::stod(val);
                else if (key == "softening")    cfg.softening    = std::stod(val);
                else if (key == "dt")           cfg.dt           = std::stod(val);
                else if (key == "timescale")    cfg.timeScale    = std::stod(val);
                else if (key == "zeromomentum") cfg.zeroMomentum = toBool(val);
            } catch (...) { /* ignore malformed directive */ }
            continue;
        }

        // Body line: name, mass, px,py,pz, vx,vy,vz, r,g,b [, radius]
        std::vector<std::string> cols;
        std::stringstream ss(t);
        std::string cell;
        while (std::getline(ss, cell, ',')) cols.push_back(trim(cell));

        if (cols.size() < 11) {
            err = "Line " + std::to_string(lineNo) +
                  ": expected at least 11 comma-separated fields, got " +
                  std::to_string(cols.size());
            return false;
        }
        try {
            Body b;
            b.name  = cols[0].empty() ? "body" : cols[0];
            b.mass  = std::stod(cols[1]);
            b.pos   = glm::dvec3(std::stod(cols[2]), std::stod(cols[3]), std::stod(cols[4]));
            b.vel   = glm::dvec3(std::stod(cols[5]), std::stod(cols[6]), std::stod(cols[7]));
            float r = std::stof(cols[8]), g = std::stof(cols[9]), bl = std::stof(cols[10]);
            // Auto-detect 0..255 colours and normalise to 0..1.
            if (r > 1.0f || g > 1.0f || bl > 1.0f) { r /= 255.f; g /= 255.f; bl /= 255.f; }
            b.color = glm::vec3(r, g, bl);
            if (cols.size() >= 12 && !cols[11].empty()) b.radius = std::stod(cols[11]);
            out.push_back(std::move(b));
        } catch (const std::exception&) {
            err = "Line " + std::to_string(lineNo) + ": could not parse numbers.";
            return false;
        }
    }

    if (out.empty()) { err = "Scene file contained no bodies."; return false; }
    return true;
}
