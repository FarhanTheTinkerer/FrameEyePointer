#pragma once

#include "eyepointer/DesktopMapping.h"
#include "eyepointer/GazeFilter.h"
#include "eyepointer/PointerController.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eyepointer {

/// An overlay the eye pointer can land on.
struct TargetConfig {
    enum class Mode {
        /// The overlay shows a Windows desktop: the gaze drives the OS mouse.
        Desktop,
        /// Only show the reticle on it (for overlays we cannot send clicks to,
        /// such as the SteamVR dashboard).
        Pointer,
    };
    std::string name;
    /// Overlay key. A trailing '*' matches key0 .. key31 (Desktop+ numbers its overlays).
    std::string key;
    Mode mode = Mode::Pointer;
    DesktopSelector desktop;
};

struct Settings {
    // [pointer]
    bool enabledAtStart = true;
    float reticleDegrees = 1.0f;     ///< Angular size of the reticle.
    uint32_t reticleColor = 0x00E5FF; ///< RRGGBB
    float reticleAlpha = 0.9f;
    bool reticleWhenNoTarget = true;  ///< Also show it in free space.
    /// Free-space depth: true = use the eye-tracker fixation distance, false = freeDepthMeters.
    bool freeDepthFromFixation = true;
    float freeDepthMeters = 2.0f;
    float minDepthMeters = 0.3f;
    float maxDepthMeters = 10.0f;
    bool hapticOnClick = true;

    // [filter]
    GazeFilterParams filter;

    // [mouse]
    bool mouseEnabled = true;
    PointerParams pointer;

    // [target.*]
    std::vector<TargetConfig> targets;

    /// Parses INI text. Unknown or malformed entries are reported in `warnings` and
    /// ignored. If the text has no [target.*] sections the default targets are used.
    static Settings parse(const std::string& text, std::vector<std::string>* warnings = nullptr);

    /// The commented INI file written on first run.
    static const char* defaultIni();
};

} // namespace eyepointer
