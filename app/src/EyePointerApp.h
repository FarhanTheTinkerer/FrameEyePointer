#pragma once

#include "eyepointer/GazeTracker.h"
#include "eyepointer/PointerController.h"
#include "eyepointer/Settings.h"

#include <openvr.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace app {

struct Options {
    bool console = false;
    bool probe = false;
    std::optional<bool> autostart; ///< --autostart on|off
    std::string configPath;        ///< empty = FrameEyePointer.ini next to the exe
};

class EyePointerApp {
public:
    static constexpr const char* kAppKey = "frameeyepointer.pc";

    explicit EyePointerApp(Options options) : options_(std::move(options)) {}
    ~EyePointerApp();

    /// Runs until SteamVR quits or `quit` is set. Returns a process exit code.
    int run(std::atomic<bool>& quit);

private:
    struct ResolvedTarget {
        vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
        std::string key;
        const eyepointer::TargetConfig* config = nullptr;
    };

    struct Hit {
        const ResolvedTarget* target = nullptr;
        vr::VROverlayIntersectionResults_t result{};
    };

    bool initVr();
    void shutdownVr();
    void registerApplication();
    bool setupInput();
    bool createReticle();
    void updateReticleImage();

    void loadSettings();
    void reloadSettingsIfChanged();
    void refreshTargets();
    void refreshDisplayTiming();

    bool pollEvents(); ///< false when SteamVR asked us to quit
    void frame();
    eyepointer::RawGaze readGaze();
    bool headPose(float secondsFromNow, eyepointer::Mat34& out);
    std::optional<Hit> intersectTargets(const eyepointer::GazeRay& ray);
    std::optional<eyepointer::Point> desktopPixel(const Hit& hit);
    void showReticle(const eyepointer::Vec3& point, const eyepointer::Vec3& eye);
    void hideReticle();
    void applyMouse(const std::vector<eyepointer::MouseCommand>& commands);

    bool digital(vr::VRActionHandle_t action, bool* risingEdge = nullptr,
                 vr::VRInputValueHandle_t* origin = nullptr);

    int probe(std::atomic<bool>& quit);

    Options options_;
    std::string exeDir_;
    std::filesystem::path configPath_;
    std::filesystem::file_time_type configTime_{};
    eyepointer::Settings settings_;

    bool vrStarted_ = false;
    bool inputOk_ = false;
    vr::VRActionSetHandle_t actionSet_ = vr::k_ulInvalidActionSetHandle;
    vr::VRActionHandle_t gazeAction_ = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t clickLeftAction_ = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t clickRightAction_ = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t toggleAction_ = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t scrollAction_ = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t hapticAction_ = vr::k_ulInvalidActionHandle;

    vr::VROverlayHandle_t reticle_ = vr::k_ulOverlayHandleInvalid;
    bool reticleVisible_ = false;

    std::vector<ResolvedTarget> targets_;
    std::vector<eyepointer::Monitor> monitors_;
    std::set<std::string> warnedKeys_;

    eyepointer::GazeTracker tracker_;
    eyepointer::PointerController pointer_;
    bool enabled_ = true;

    float frameSeconds_ = 1.0f / 90.0f;
    float vsyncToPhotons_ = 0.0f;

    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    double lastSlowTick_ = -1e9;
    double lastConfigCheck_ = -1e9;

    // Diagnostics, logged every few seconds.
    int framesSinceReport_ = 0;
    int validSinceReport_ = 0;
    double lastReport_ = 0;
    bool loggedGazeError_ = false;
};

} // namespace app
