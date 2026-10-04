#pragma once

#include "eyepointer/ClickSequencer.h"
#include "eyepointer/GazeFilter.h"

#include <string>
#include <vector>

namespace eyepointer {

/// Settings of the on-headset service (frameeyepointerd),
/// ~/.config/frameeyepointer/frameeyepointer.ini.
struct FrameSettings {
    enum class GazeSource {
        /// SteamVR's eyetracking action outside games, the eye tracker's shared
        /// memory during games (reading the action in a game makes SteamVR restart
        /// its eye tracker).
        Auto,
        Action, ///< only the SteamVR action (never during games)
        Mmap,   ///< only the shared memory
    };

    // [inputs]
    /// Aim sources in priority order: the first one pointing wins. "eyes" is built in;
    /// any other name is an external source (see InputSources.h), e.g. "hands".
    std::vector<std::string> aimSources = {"eyes"};
    /// Button sources, combined. "controllers" is built in; other names are external.
    std::vector<std::string> buttonSources = {"controllers"};
    /// An external source that sends nothing for this long is treated as gone.
    double externalTimeoutSeconds = 0.25;
    /// Smooth external aims with the [filter] settings (off: the source smooths itself).
    bool smoothExternal = false;

    // [pointer]
    bool enabledAtStart = true;
    GazeSource gazeSource = GazeSource::Auto;
    /// Keep the eye laser on the SteamVR dashboard while a game runs.
    bool dashboardInGames = true;
    /// Keep the last gaze this long through blinks and dropouts.
    double holdGazeSeconds = 1.0;
    /// Show an invisible overlay that keeps SteamVR in laser mode while the eye laser is on.
    bool forceLaserMode = true;
    /// Eye openness below this on both eyes counts as a blink (shared-memory source).
    float blinkOpenness = 0.15f;

    // [filter]
    GazeFilterParams filter;

    // [click]
    ClickTiming click;
    /// Turn on SteamVR's "Enable global input from overlays" so the controller buttons
    /// reach the service while other apps have input focus.
    bool globalInput = true;
    /// Hold the toggle button (left View by default) this long to turn the eye laser on/off.
    double toggleHoldSeconds = 0.6;

    static FrameSettings parse(const std::string& text, std::vector<std::string>* warnings = nullptr);
    static const char* defaultIni();
};

} // namespace eyepointer
