#include "EyePointerApp.h"

#include "Log.h"
#include "Platform.h"

#include "eyepointer/Reticle.h"
#include "eyepointer/openvr/OpenVrGaze.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <thread>

namespace app {

using eyepointer::GazeRay;
using eyepointer::Mat34;
using eyepointer::MouseCommand;
using eyepointer::RawGaze;
using eyepointer::TargetConfig;
using eyepointer::Vec3;
using eyepointer::openvr::toHmd;
using eyepointer::openvr::toVec;

namespace {

constexpr const char* kReticleKey = "farhanthetinkerer.frameeyepointer.reticle";
constexpr int kReticlePixels = 128;
constexpr int kMaxWildcardIndex = 32;

std::string pathString(const std::filesystem::path& p) {
#ifdef _WIN32
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
#else
    return p.string();
#endif
}

std::string stringProp(vr::TrackedDeviceIndex_t device, vr::ETrackedDeviceProperty prop) {
    char buf[vr::k_unMaxPropertyStringSize] = {};
    vr::ETrackedPropertyError err = vr::TrackedProp_Success;
    vr::VRSystem()->GetStringTrackedDeviceProperty(device, prop, buf, sizeof buf, &err);
    return err == vr::TrackedProp_Success ? std::string(buf) : std::string("<unavailable>");
}

const char* inputErrorName(vr::EVRInputError e) {
    switch (e) {
    case vr::VRInputError_None: return "None";
    case vr::VRInputError_NameNotFound: return "NameNotFound";
    case vr::VRInputError_WrongType: return "WrongType";
    case vr::VRInputError_InvalidHandle: return "InvalidHandle";
    case vr::VRInputError_InvalidParam: return "InvalidParam";
    case vr::VRInputError_NoSteam: return "NoSteam";
    case vr::VRInputError_MaxCapacityReached: return "MaxCapacityReached";
    case vr::VRInputError_IPCError: return "IPCError";
    case vr::VRInputError_NoActiveActionSet: return "NoActiveActionSet";
    case vr::VRInputError_InvalidDevice: return "InvalidDevice";
    case vr::VRInputError_InvalidSkeleton: return "InvalidSkeleton";
    case vr::VRInputError_InvalidBoneCount: return "InvalidBoneCount";
    case vr::VRInputError_InvalidCompressedData: return "InvalidCompressedData";
    case vr::VRInputError_NoData: return "NoData";
    case vr::VRInputError_BufferTooSmall: return "BufferTooSmall";
    case vr::VRInputError_MismatchedActionManifest: return "MismatchedActionManifest";
    case vr::VRInputError_MissingSkeletonData: return "MissingSkeletonData";
    case vr::VRInputError_InvalidBoneIndex: return "InvalidBoneIndex";
    case vr::VRInputError_InvalidPriority: return "InvalidPriority";
    case vr::VRInputError_PermissionDenied: return "PermissionDenied";
    case vr::VRInputError_InvalidRenderModel: return "InvalidRenderModel";
    }
    return "Unknown";
}

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

EyePointerApp::~EyePointerApp() { shutdownVr(); }

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void EyePointerApp::loadSettings() {
    std::error_code ec;
    if (!std::filesystem::exists(configPath_, ec)) {
        std::ofstream out(configPath_, std::ios::binary);
        out << eyepointer::Settings::defaultIni();
        logf("Wrote default settings to %s", pathString(configPath_).c_str());
    }
    std::ifstream in(configPath_, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();

    std::vector<std::string> warnings;
    settings_ = eyepointer::Settings::parse(text.str(), &warnings);
    for (const auto& w : warnings) logf("Settings: %s", w.c_str());
    configTime_ = std::filesystem::last_write_time(configPath_, ec);

    tracker_.setFilterParams(settings_.filter);
    pointer_.setParams(settings_.pointer);
    logf("Settings loaded: %u target(s), mouse %s", static_cast<unsigned>(settings_.targets.size()),
         settings_.mouseEnabled ? "on" : "off");
}

void EyePointerApp::reloadSettingsIfChanged() {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(configPath_, ec);
    if (ec || t == configTime_) return;
    logf("Settings file changed, reloading");
    loadSettings();
    updateReticleImage();
    warnedKeys_.clear();
    refreshTargets();
}

// ---------------------------------------------------------------------------
// OpenVR setup
// ---------------------------------------------------------------------------

bool EyePointerApp::initVr() {
    vr::EVRInitError err = vr::VRInitError_None;
    vr::VR_Init(&err, vr::VRApplication_Overlay);
    if (err != vr::VRInitError_None) {
        logf("Could not connect to SteamVR: %s", vr::VR_GetVRInitErrorAsEnglishDescription(err));
        return false;
    }
    vrStarted_ = true;
    logf("Connected to SteamVR. HMD: %s (%s)",
         stringProp(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_ModelNumber_String).c_str(),
         stringProp(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_ControllerType_String).c_str());

    registerApplication();
    inputOk_ = setupInput();
    if (!createReticle()) return false;
    refreshDisplayTiming();
    return true;
}

void EyePointerApp::shutdownVr() {
    if (!vrStarted_) return;
    applyMouse(pointer_.releaseAll());
    if (reticle_ != vr::k_ulOverlayHandleInvalid) vr::VROverlay()->DestroyOverlay(reticle_);
    reticle_ = vr::k_ulOverlayHandleInvalid;
    vr::VR_Shutdown();
    vrStarted_ = false;
}

void EyePointerApp::registerApplication() {
    std::filesystem::path manifest =
        std::filesystem::u8path(exeDir_) / "frameeyepointer.vrmanifest";
    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec)) {
        logf("No frameeyepointer.vrmanifest next to the executable; running unregistered");
        return;
    }
    auto* apps = vr::VRApplications();
    vr::EVRApplicationError e = apps->AddApplicationManifest(pathString(manifest).c_str(), false);
    if (e != vr::VRApplicationError_None) {
        logf("Registering with SteamVR failed: %s", apps->GetApplicationsErrorNameFromEnum(e));
        return;
    }
    e = apps->IdentifyApplication(platform::processId(), kAppKey);
    if (e != vr::VRApplicationError_None)
        logf("IdentifyApplication failed: %s", apps->GetApplicationsErrorNameFromEnum(e));

    if (options_.autostart) {
        e = apps->SetApplicationAutoLaunch(kAppKey, *options_.autostart);
        logf("Start with SteamVR: %s (%s)", *options_.autostart ? "on" : "off",
             apps->GetApplicationsErrorNameFromEnum(e));
    }
}

bool EyePointerApp::setupInput() {
    auto* input = vr::VRInput();
    std::string manifest = pathString(std::filesystem::u8path(exeDir_) / "actions.json");
    vr::EVRInputError e = input->SetActionManifestPath(manifest.c_str());
    if (e != vr::VRInputError_None) {
        logf("Loading %s failed: %s. Eye input is unavailable.", manifest.c_str(),
             inputErrorName(e));
        return false;
    }
    bool ok = input->GetActionSetHandle("/actions/eyepointer", &actionSet_) == vr::VRInputError_None;
    ok &= input->GetActionHandle("/actions/eyepointer/in/gaze", &gazeAction_) == vr::VRInputError_None;
    ok &= input->GetActionHandle("/actions/eyepointer/in/click_left", &clickLeftAction_) == vr::VRInputError_None;
    ok &= input->GetActionHandle("/actions/eyepointer/in/click_right", &clickRightAction_) == vr::VRInputError_None;
    ok &= input->GetActionHandle("/actions/eyepointer/in/toggle", &toggleAction_) == vr::VRInputError_None;
    ok &= input->GetActionHandle("/actions/eyepointer/in/scroll", &scrollAction_) == vr::VRInputError_None;
    ok &= input->GetActionHandle("/actions/eyepointer/out/haptic", &hapticAction_) == vr::VRInputError_None;
    if (!ok) logf("Some actions could not be found in actions.json");
    return ok;
}

bool EyePointerApp::createReticle() {
    auto* ov = vr::VROverlay();
    vr::EVROverlayError e = ov->CreateOverlay(kReticleKey, "Eye Pointer", &reticle_);
    if (e == vr::VROverlayError_KeyInUse) {
        logf("Another FrameEyePointer is already running");
        return false;
    }
    if (e != vr::VROverlayError_None) {
        logf("Creating the pointer overlay failed: %s", ov->GetOverlayErrorNameFromEnum(e));
        return false;
    }
    ov->SetOverlaySortOrder(reticle_, 200); // above other overlays
    updateReticleImage();
    return true;
}

void EyePointerApp::updateReticleImage() {
    if (reticle_ == vr::k_ulOverlayHandleInvalid) return;
    auto pixels = eyepointer::makeReticleImage(kReticlePixels, settings_.reticleColor,
                                               settings_.reticleAlpha);
    vr::VROverlay()->SetOverlayRaw(reticle_, pixels.data(), kReticlePixels, kReticlePixels, 4);
}

void EyePointerApp::refreshDisplayTiming() {
    auto* sys = vr::VRSystem();
    vr::ETrackedPropertyError err = vr::TrackedProp_Success;
    float hz = sys->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
                                                  vr::Prop_DisplayFrequency_Float, &err);
    if (err == vr::TrackedProp_Success && hz > 20.0f) frameSeconds_ = 1.0f / hz;
    float photons = sys->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
                                                       vr::Prop_SecondsFromVsyncToPhotons_Float, &err);
    if (err == vr::TrackedProp_Success && photons >= 0.0f && photons < 0.1f) vsyncToPhotons_ = photons;
}

void EyePointerApp::refreshTargets() {
    auto* ov = vr::VROverlay();
    std::vector<ResolvedTarget> found;
    for (const auto& t : settings_.targets) {
        auto add = [&](const std::string& key) {
            vr::VROverlayHandle_t h = vr::k_ulOverlayHandleInvalid;
            if (ov->FindOverlay(key.c_str(), &h) == vr::VROverlayError_None &&
                h != vr::k_ulOverlayHandleInvalid && h != reticle_) {
                found.push_back({h, key, &t});
            }
        };
        if (!t.key.empty() && t.key.back() == '*') {
            std::string prefix = t.key.substr(0, t.key.size() - 1);
            add(prefix);
            for (int i = 0; i < kMaxWildcardIndex; ++i) add(prefix + std::to_string(i));
        } else {
            add(t.key);
        }
    }

    bool changed = found.size() != targets_.size();
    for (size_t i = 0; !changed && i < found.size(); ++i)
        changed = found[i].handle != targets_[i].handle;
    if (changed) {
        std::string keys;
        for (const auto& f : found) keys += (keys.empty() ? "" : ", ") + f.key;
        logf("Target overlays: %s", keys.empty() ? "(none found yet)" : keys.c_str());
    }
    targets_ = std::move(found);

    auto mons = platform::monitors();
    if (mons.size() != monitors_.size() ||
        !std::equal(mons.begin(), mons.end(), monitors_.begin(),
                    [](const auto& a, const auto& b) { return a.rect == b.rect; })) {
        for (size_t i = 0; i < mons.size(); ++i)
            logf("Monitor %u: %d,%d %dx%d%s", static_cast<unsigned>(i), mons[i].rect.x, mons[i].rect.y,
                 mons[i].rect.width, mons[i].rect.height, mons[i].primary ? " (primary)" : "");
    }
    monitors_ = std::move(mons);
}

// ---------------------------------------------------------------------------
// Per-frame work
// ---------------------------------------------------------------------------

bool EyePointerApp::pollEvents() {
    vr::VREvent_t ev;
    while (vr::VRSystem()->PollNextEvent(&ev, sizeof ev)) {
        switch (ev.eventType) {
        case vr::VREvent_Quit:
            logf("SteamVR is shutting down");
            vr::VRSystem()->AcknowledgeQuit_Exiting();
            return false;
        case vr::VREvent_ActionBindingReloaded:
            logf("Controller bindings reloaded");
            break;
        case vr::VREvent_TrackedDeviceActivated:
        case vr::VREvent_TrackedDeviceUpdated:
            refreshDisplayTiming();
            break;
        default:
            break;
        }
    }
    return true;
}

bool EyePointerApp::digital(vr::VRActionHandle_t action, bool* risingEdge,
                            vr::VRInputValueHandle_t* origin) {
    if (risingEdge) *risingEdge = false;
    if (!inputOk_ || action == vr::k_ulInvalidActionHandle) return false;
    vr::InputDigitalActionData_t d{};
    if (vr::VRInput()->GetDigitalActionData(action, &d, sizeof d, vr::k_ulInvalidInputValueHandle) !=
            vr::VRInputError_None ||
        !d.bActive)
        return false;
    if (risingEdge) *risingEdge = d.bState && d.bChanged;
    if (origin) *origin = d.activeOrigin;
    return d.bState;
}

RawGaze EyePointerApp::readGaze() {
    RawGaze g;
    if (!inputOk_ || gazeAction_ == vr::k_ulInvalidActionHandle) return g;
    vr::EVRInputError e = vr::VRInputError_None;
    g = eyepointer::openvr::readGaze(gazeAction_, vr::TrackingUniverseStanding, 0.0f, &e);
    if (e != vr::VRInputError_None) {
        if (!loggedGazeError_) {
            logf("Reading eye tracking failed: %s (is eye tracking enabled in SteamVR?)",
                 inputErrorName(e));
            loggedGazeError_ = true;
        }
        return g;
    }
    loggedGazeError_ = false;
    return g;
}

bool EyePointerApp::headPose(float secondsFromNow, Mat34& out) {
    return eyepointer::openvr::hmdPose(secondsFromNow, out);
}

std::optional<EyePointerApp::Hit> EyePointerApp::intersectTargets(const GazeRay& ray) {
    auto* ov = vr::VROverlay();
    std::optional<Hit> best;
    vr::VROverlayIntersectionParams_t params{};
    params.vSource = toHmd(ray.origin);
    params.vDirection = toHmd(ray.direction);
    params.eOrigin = vr::TrackingUniverseStanding;
    for (const auto& t : targets_) {
        if (!ov->IsOverlayVisible(t.handle)) continue;
        vr::VROverlayIntersectionResults_t r{};
        if (!ov->ComputeOverlayIntersection(t.handle, &params, &r)) continue;
        if (r.fDistance <= 0.0f) continue;
        if (eyepointer::dot(toVec(r.vNormal), ray.direction) >= 0.0f) continue; // back face
        if (!best || r.fDistance < best->result.fDistance) best = Hit{&t, r};
    }
    return best;
}

std::optional<eyepointer::Point> EyePointerApp::desktopPixel(const Hit& hit) {
    const TargetConfig& cfg = *hit.target->config;
    if (cfg.mode != TargetConfig::Mode::Desktop) return std::nullopt;

    vr::HmdVector2_t scale{};
    if (vr::VROverlay()->GetOverlayMouseScale(hit.target->handle, &scale) != vr::VROverlayError_None)
        return std::nullopt;
    auto rect = eyepointer::chooseDesktopRect(cfg.desktop, scale.v[0], scale.v[1], monitors_);
    if (!rect) {
        if (warnedKeys_.insert(hit.target->key).second)
            logf("Overlay %s is %.0fx%.0f, which matches no monitor layout. Set 'desktop =' for "
                 "[target.%s] (monitor:N or rect:x,y,w,h).",
                 hit.target->key.c_str(), scale.v[0], scale.v[1], cfg.name.c_str());
        return std::nullopt;
    }
    return eyepointer::uvToDesktopPixel(hit.result.vUVs.v[0], hit.result.vUVs.v[1], *rect,
                                        monitors_);
}

void EyePointerApp::showReticle(const Vec3& point, const Vec3& eye) {
    float distance = eyepointer::length(point - eye);
    float width = 2.0f * distance * std::tan(settings_.reticleDegrees * 0.5f * 3.14159265f / 180.0f);
    auto* ov = vr::VROverlay();
    ov->SetOverlayWidthInMeters(reticle_, std::max(width, 0.001f));
    vr::HmdMatrix34_t m = toHmd(eyepointer::billboard(point, eye));
    ov->SetOverlayTransformAbsolute(reticle_, vr::TrackingUniverseStanding, &m);
    if (!reticleVisible_) {
        ov->ShowOverlay(reticle_);
        reticleVisible_ = true;
    }
}

void EyePointerApp::hideReticle() {
    if (!reticleVisible_) return;
    vr::VROverlay()->HideOverlay(reticle_);
    reticleVisible_ = false;
}

void EyePointerApp::applyMouse(const std::vector<MouseCommand>& commands) {
    if (commands.empty() || !platform::mouseSupported()) return;
    platform::sendMouse(commands);
}

void EyePointerApp::frame() {
    double now = secondsSince(start_);

    if (now - lastConfigCheck_ > 1.0) {
        lastConfigCheck_ = now;
        reloadSettingsIfChanged();
    }
    if (now - lastSlowTick_ > 2.0) {
        lastSlowTick_ = now;
        refreshTargets();
    }

    if (inputOk_) {
        vr::VRActiveActionSet_t active{};
        active.ulActionSet = actionSet_;
        vr::VRInput()->UpdateActionState(&active, sizeof active, 1);
    }

    bool toggled = false;
    digital(toggleAction_, &toggled);
    if (toggled) {
        enabled_ = !enabled_;
        logf("Eye pointer %s", enabled_ ? "on" : "off");
    }

    vr::VRInputValueHandle_t leftOrigin = vr::k_ulInvalidInputValueHandle;
    vr::VRInputValueHandle_t rightOrigin = vr::k_ulInvalidInputValueHandle;
    bool leftHeld = digital(clickLeftAction_, nullptr, &leftOrigin);
    bool rightHeld = digital(clickRightAction_, nullptr, &rightOrigin);
    float scroll = 0;
    if (inputOk_) {
        vr::InputAnalogActionData_t a{};
        if (vr::VRInput()->GetAnalogActionData(scrollAction_, &a, sizeof a,
                                               vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None &&
            a.bActive)
            scroll = a.y;
    }

    // Gaze relative to the head now; drawn with the head pose predicted for display.
    float sinceVsync = 0;
    uint64_t frameCounter = 0;
    vr::VRSystem()->GetTimeSinceLastVsync(&sinceVsync, &frameCounter);
    float predict = std::clamp(frameSeconds_ - sinceVsync + vsyncToPhotons_, 0.0f, 0.05f);

    RawGaze raw = readGaze();
    Mat34 headNow, headDisplay;
    bool headOk = headPose(0.0f, headNow) && headPose(predict, headDisplay);
    GazeRay ray;
    if (headOk) ray = tracker_.update(raw, headNow, headDisplay, now);
    else tracker_.reset();

    ++framesSinceReport_;
    if (ray.valid) ++validSinceReport_;
    if (now - lastReport_ > 10.0) {
        logf("Gaze valid in %d of %d frames over the last %.0f s", validSinceReport_,
             framesSinceReport_, now - lastReport_);
        framesSinceReport_ = validSinceReport_ = 0;
        lastReport_ = now;
    }

    std::optional<eyepointer::Point> pixel;
    if (enabled_ && ray.valid) {
        auto hit = intersectTargets(ray);
        if (hit) {
            showReticle(toVec(hit->result.vPoint), ray.origin);
            pixel = desktopPixel(*hit);
        } else if (settings_.reticleWhenNoTarget) {
            float depth = settings_.freeDepthFromFixation ? ray.fixationDistance
                                                          : settings_.freeDepthMeters;
            depth = std::clamp(depth, settings_.minDepthMeters, settings_.maxDepthMeters);
            showReticle(ray.origin + ray.direction * depth, ray.origin);
        } else {
            hideReticle();
        }
    } else {
        hideReticle();
    }

    eyepointer::PointerInput in;
    in.timeSeconds = now;
    in.enabled = enabled_ && settings_.mouseEnabled;
    in.gazePixel = pixel;
    in.leftHeld = leftHeld;
    in.rightHeld = rightHeld;
    in.scroll = scroll;
    auto commands = pointer_.update(in);
    applyMouse(commands);

    if (settings_.hapticOnClick && inputOk_) {
        for (const auto& c : commands) {
            if (c.type != MouseCommand::Type::ButtonDown) continue;
            auto origin = c.button == eyepointer::MouseButton::Left ? leftOrigin : rightOrigin;
            vr::VRInput()->TriggerHapticVibrationAction(hapticAction_, 0.0f, 0.02f, 160.0f, 0.3f,
                                                         origin);
        }
    }
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

int EyePointerApp::run(std::atomic<bool>& quit) {
    exeDir_ = platform::executableDir();
    configPath_ = options_.configPath.empty()
                      ? std::filesystem::u8path(exeDir_) / "FrameEyePointer.ini"
                      : std::filesystem::u8path(options_.configPath);
    loadSettings();
    enabled_ = settings_.enabledAtStart;

    if (!initVr()) return 1;
    if (options_.probe) return probe(quit);

    if (!platform::mouseSupported())
        logf("Desktop mouse control is not available on this platform; pointer only");
    logf("Running. Right bumper = left click, left bumper = right click, hold left View = on/off.");

    while (!quit.load()) {
        auto t0 = std::chrono::steady_clock::now();
        // Paces the loop to the compositor; falls back to a timer if unavailable.
        vr::EVROverlayError sync = vr::VROverlay()->WaitFrameSync(50);
        if (!pollEvents()) break;
        frame();
        if (sync != vr::VROverlayError_None) {
            auto spent = std::chrono::steady_clock::now() - t0;
            auto target = std::chrono::duration<float>(frameSeconds_);
            if (spent < target) std::this_thread::sleep_for(target - spent);
        }
    }
    logf("Exiting");
    return 0;
}

// ---------------------------------------------------------------------------
// --probe: report what this system exposes, then exit
// ---------------------------------------------------------------------------

int EyePointerApp::probe(std::atomic<bool>& quit) {
    auto* sys = vr::VRSystem();
    auto* ov = vr::VROverlay();
    logf("==== FrameEyePointer probe ====");

    vr::ETrackedPropertyError perr = vr::TrackedProp_Success;
    bool eyeGaze = sys->GetBoolTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
                                                     vr::Prop_SupportsXrEyeGazeInteraction_Bool, &perr);
    logf("HMD supports eye gaze: %s", perr == vr::TrackedProp_Success ? (eyeGaze ? "yes" : "no")
                                                                      : "unknown");
    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (!sys->IsTrackedDeviceConnected(i)) continue;
        logf("Device %u: class %d, controller_type '%s', model '%s'", i,
             static_cast<int>(sys->GetTrackedDeviceClass(i)),
             stringProp(i, vr::Prop_ControllerType_String).c_str(),
             stringProp(i, vr::Prop_ModelNumber_String).c_str());
    }

    logf("Display: %.1f Hz, vsync-to-photons %.1f ms", 1.0f / frameSeconds_, vsyncToPhotons_ * 1000);

    // Eye tracking for 3 seconds.
    int samples = 0, valid = 0;
    vr::VREyeTrackingData_t last{};
    vr::EVRInputError lastErr = vr::VRInputError_None;
    for (int i = 0; i < 90 && !quit.load(); ++i) {
        if (inputOk_) {
            vr::VRActiveActionSet_t active{};
            active.ulActionSet = actionSet_;
            vr::VRInput()->UpdateActionState(&active, sizeof active, 1);
            vr::VREyeTrackingData_t d{};
            lastErr = vr::VRInput()->GetEyeTrackingDataRelativeToNow(
                gazeAction_, vr::TrackingUniverseStanding, 0.0f, &d, sizeof d);
            ++samples;
            if (lastErr == vr::VRInputError_None && d.bActive && d.bValid && d.bTracked) {
                ++valid;
                last = d;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    logf("Eye tracking: %d of %d samples valid (last error: %s)", valid, samples,
         inputErrorName(lastErr));
    if (valid > 0) {
        Vec3 o = toVec(last.vGazeOrigin), t = toVec(last.vGazeTarget);
        logf("  last origin (%.3f, %.3f, %.3f) target (%.3f, %.3f, %.3f), distance %.3f m", o.x,
             o.y, o.z, t.x, t.y, t.z, eyepointer::length(t - o));
    }

    // Overlays: known keys, then a scan of nearby handle values.
    auto describe = [&](vr::VROverlayHandle_t h, const std::string& key) {
        vr::HmdVector2_t scale{};
        ov->GetOverlayMouseScale(h, &scale);
        float width = 0;
        ov->GetOverlayWidthInMeters(h, &width);
        char name[vr::k_unVROverlayMaxNameLength] = {};
        ov->GetOverlayName(h, name, sizeof name);
        logf("  overlay '%s' (\"%s\") handle 0x%llx visible=%d mouse=%.0fx%.0f width=%.2fm",
             key.c_str(), name, static_cast<unsigned long long>(h), ov->IsOverlayVisible(h) ? 1 : 0,
             scale.v[0], scale.v[1], width);
    };

    logf("Overlays found by key:");
    std::vector<std::string> keys = {"system.vrdashboard", "system.systemui",
                                     vr::k_pchHeadsetViewOverlayKey, "valve.steam.desktop",
                                     "valve.steam.bigpicture", "elvissteinjr.DesktopPlusDashboard"};
    for (int i = 0; i < kMaxWildcardIndex; ++i) keys.push_back("elvissteinjr.DesktopPlus" + std::to_string(i));
    for (const auto& t : settings_.targets)
        if (!t.key.empty() && t.key.back() != '*') keys.push_back(t.key);
    std::set<vr::VROverlayHandle_t> seen;
    for (const auto& k : keys) {
        vr::VROverlayHandle_t h = vr::k_ulOverlayHandleInvalid;
        if (ov->FindOverlay(k.c_str(), &h) == vr::VROverlayError_None && seen.insert(h).second)
            describe(h, k);
    }

    logf("Overlays found by handle scan (own handle 0x%llx):",
         static_cast<unsigned long long>(reticle_));
    auto scan = [&](vr::VROverlayHandle_t from, vr::VROverlayHandle_t to) {
        for (vr::VROverlayHandle_t h = from; h < to && !quit.load(); ++h) {
            char key[vr::k_unVROverlayMaxKeyLength] = {};
            vr::EVROverlayError e = vr::VROverlayError_None;
            ov->GetOverlayKey(h, key, sizeof key, &e);
            if (e == vr::VROverlayError_None && key[0] && seen.insert(h).second) describe(h, key);
        }
    };
    scan(1, 4096);
    vr::VROverlayHandle_t own = reticle_;
    scan(own > 2048 ? own - 2048 : 1, own + 2048);

    auto mons = platform::monitors();
    for (size_t i = 0; i < mons.size(); ++i)
        logf("Monitor %u: %d,%d %dx%d%s", static_cast<unsigned>(i), mons[i].rect.x, mons[i].rect.y, mons[i].rect.width,
             mons[i].rect.height, mons[i].primary ? " (primary)" : "");
    logf("==== end of probe (also saved to FrameEyePointer.log) ====");
    return valid > 0 ? 0 : 2;
}

} // namespace app
