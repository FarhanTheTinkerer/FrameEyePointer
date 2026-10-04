// frameeyepointerd: the Steam Frame eye laser's service. Runs on the headset as a
// systemd user service that starts and stops with SteamVR.
//
// Every frame it asks its input sources where to point and whether to click,
// and drives the frameeyepointer SteamVR driver, whose virtual controller carries
// SteamVR's laser. Sources are modules (InputSource.h): "eyes" and "controllers"
// are built in, and any other name is an external process (a hand tracker)
// talking to it over a socket (ExternalInput.h). [inputs] in the settings picks
// them and their priority.
//
// Steam reads the Frame controllers itself and takes SteamVR out of laser mode
// about 40 ms after every press and release (Frametop, docs/gaze-controllers.md).
// So in tap mode a press sends a whole click at once, and after each press and
// release the laser is taken back (eyepointer::ClickSequencer).
//
// Usage: frameeyepointerd [--probe] [--verbose] [--config FILE]
#include "ControllerButtonSource.h"
#include "ExternalInput.h"
#include "EyeGazeSource.h"
#include "InputSource.h"

#include "eyepointer/ClickSequencer.h"
#include "eyepointer/FrameSettings.h"
#include "eyepointer/GazeFilter.h"
#include "eyepointer/InputSources.h"
#include "eyepointer/openvr/OpenVrGaze.h"

#include <openvr.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <limits.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifndef FRAMEEYEPOINTER_VERSION
#define FRAMEEYEPOINTER_VERSION "dev"
#endif

namespace frame {

bool g_verbose = false;

void logf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::putchar('\n');
    std::fflush(stdout);
}

} // namespace frame

namespace {

using eyepointer::Aim;
using eyepointer::FrameSettings;
using eyepointer::Mat34;
using eyepointer::Vec3;
using frame::logf;
namespace fs = std::filesystem;

std::atomic<bool> g_quit{false};
void onSignal(int) { g_quit = true; }

double now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

std::string executableDir() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    std::string p(buf, size_t(n));
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? "." : p.substr(0, slash);
}

fs::path defaultConfigPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    fs::path base = xdg && *xdg ? fs::path(xdg) : fs::path(home ? home : ".") / ".config";
    return base / "frameeyepointer" / "frameeyepointer.ini";
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string osRelease() {
    std::ifstream in("/etc/os-release");
    std::string line, pretty, build;
    auto value = [&](const char* key) {
        std::string k = std::string(key) + "=";
        if (line.rfind(k, 0) != 0) return std::string();
        std::string v = line.substr(k.size());
        if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2);
        return v;
    };
    while (std::getline(in, line)) {
        if (auto v = value("PRETTY_NAME"); !v.empty()) pretty = v;
        if (auto v = value("BUILD_ID"); !v.empty()) build = v;
    }
    return pretty + (build.empty() ? "" : " (build " + build + ")");
}

std::string stringProp(vr::TrackedDeviceIndex_t i, vr::ETrackedDeviceProperty p) {
    char buf[vr::k_unMaxPropertyStringSize] = {};
    vr::ETrackedPropertyError err = vr::TrackedProp_Success;
    vr::VRSystem()->GetStringTrackedDeviceProperty(i, p, buf, sizeof buf, &err);
    return err == vr::TrackedProp_Success ? buf : "";
}

/// The driver's control socket (see frame/driver).
class DriverLink {
public:
    DriverLink() {
        fd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        addr_.sun_family = AF_UNIX;
        const char name[] = "frameeyepointer";
        std::memcpy(addr_.sun_path + 1, name, sizeof name - 1);
        len_ = socklen_t(offsetof(sockaddr_un, sun_path) + 1 + sizeof name - 1);
    }
    ~DriverLink() {
        if (fd_ >= 0) close(fd_);
    }
    void send(const char* fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
    {
        char msg[256];
        va_list args;
        va_start(args, fmt);
        int n = std::vsnprintf(msg, sizeof msg, fmt, args);
        va_end(args);
        if (n <= 0) return;
        if (frame::g_verbose && std::strncmp(msg, "gaze", 4) != 0 && std::strncmp(msg, "ray", 3) != 0)
            logf("-> driver: %s", msg);
        sendto(fd_, msg, size_t(n), 0, reinterpret_cast<sockaddr*>(&addr_), len_);
    }

private:
    int fd_ = -1;
    sockaddr_un addr_{};
    socklen_t len_ = 0;
};

class Service {
public:
    Service(fs::path config, bool probe) : configPath_(std::move(config)), probe_(probe) {}
    int run();

private:
    bool connect();
    void setupInput();
    void ensureGlobalInput();
    void updateLaserMode();
    void loadSettings();
    void reloadSettingsIfChanged();
    void buildSources();
    frame::InputSource* source(const std::string& name);
    void findOurDevice();
    bool pollEvents();
    frame::FrameContext context(double t);
    void frame(double t);
    void sendAim(const std::string& from, Aim aim, const frame::FrameContext& ctx);
    void setActive(bool active, double t);
    int probe();

    fs::path configPath_;
    bool probe_;
    fs::file_time_type configTime_{};
    FrameSettings settings_;

    vr::IVRSystem* sys_ = nullptr;
    bool manifestOk_ = false;
    vr::VROverlayHandle_t laserMode_ = vr::k_ulOverlayHandleInvalid;
    bool laserModeShown_ = false;

    DriverLink driver_;
    vr::TrackedDeviceIndex_t ours_ = vr::k_unTrackedDeviceIndexInvalid;

    std::unique_ptr<frame::ExternalInputHub> hub_;
    std::map<std::string, std::unique_ptr<frame::InputSource>> sources_;
    std::vector<std::string> builtFor_;
    std::map<std::string, eyepointer::GazeFilter> externalFilters_;

    eyepointer::ClickSequencer clicks_;
    bool enabled_ = true, active_ = false;
    double activeSince_ = 0;
    std::string aimFrom_;
    float sentScroll_ = 0;
    double toggleDownAt_ = 0;
    bool toggleFired_ = false;

    bool inGame_ = false, worn_ = true;
    double lastSlow_ = -1e9, lastConfigCheck_ = -1e9, lastDeviceSearch_ = -1e9;
    bool warnedNoDriver_ = false;

    double lastReport_ = 0;
    int frames_ = 0, aimFrames_ = 0, clicksSent_ = 0;
};

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void Service::loadSettings() {
    std::error_code ec;
    if (!fs::exists(configPath_, ec)) {
        fs::create_directories(configPath_.parent_path(), ec);
        std::ofstream(configPath_, std::ios::binary) << FrameSettings::defaultIni();
        logf("wrote default settings to %s", configPath_.c_str());
    }
    std::vector<std::string> warnings;
    settings_ = FrameSettings::parse(readFile(configPath_), &warnings);
    for (const auto& w : warnings) logf("settings: %s", w.c_str());
    configTime_ = fs::last_write_time(configPath_, ec);
    clicks_.setTiming(settings_.click);
}

void Service::reloadSettingsIfChanged() {
    std::error_code ec;
    auto t = fs::last_write_time(configPath_, ec);
    if (ec || t == configTime_) return;
    logf("settings changed, reloading");
    loadSettings();
    ensureGlobalInput();
    buildSources();
    updateLaserMode();
}

bool Service::connect() {
    while (!g_quit) {
        vr::EVRInitError err = vr::VRInitError_None;
        sys_ = vr::VR_Init(&err, vr::VRApplication_Overlay);
        if (err == vr::VRInitError_None) return true;
        logf("waiting for SteamVR: %s", vr::VR_GetVRInitErrorAsEnglishDescription(err));
        if (probe_) return false;
        for (int i = 0; i < 20 && !g_quit; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

void Service::setupInput() {
    std::string manifest = executableDir() + "/actions/actions.json";
    vr::EVRInputError e = vr::VRInput()->SetActionManifestPath(manifest.c_str());
    manifestOk_ = e == vr::VRInputError_None;
    if (!manifestOk_) logf("loading %s failed (error %d): no eye tracking or buttons", manifest.c_str(), int(e));
    ensureGlobalInput();
}

void Service::ensureGlobalInput() {
    bool global = vr::VRSettings()->GetBool("steamvr", "globalActionSetPriority", nullptr);
    if (global) return;
    if (settings_.globalInput) {
        vr::VRSettings()->SetBool("steamvr", "globalActionSetPriority", true);
        logf("turned on SteamVR's \"Enable global input from overlays\" (needed for the bumpers)");
    } else {
        logf("SteamVR's \"Enable global input from overlays\" is off: controller clicks only work "
             "while the eye pointer has input focus");
    }
}

frame::InputSource* Service::source(const std::string& name) {
    auto it = sources_.find(name);
    return it == sources_.end() ? nullptr : it->second.get();
}

void Service::buildSources() {
    std::vector<std::string> wanted = settings_.aimSources;
    for (const auto& b : settings_.buttonSources) wanted.push_back(b);
    if (wanted == builtFor_) return;
    builtFor_ = wanted;
    for (const auto& name : wanted) {
        if (sources_.count(name)) continue;
        std::unique_ptr<frame::InputSource> s;
        if (name == "eyes") {
            if (manifestOk_) s = std::make_unique<frame::EyeGazeSource>();
        } else if (name == "controllers") {
            if (manifestOk_) s = std::make_unique<frame::ControllerButtonSource>();
        } else {
            if (!hub_) hub_ = std::make_unique<frame::ExternalInputHub>();
            s = std::make_unique<frame::ExternalSource>(name, *hub_);
        }
        if (s) sources_[name] = std::move(s);
    }
    auto join = [](const std::vector<std::string>& v) {
        std::string out;
        for (const auto& x : v) out += (out.empty() ? "" : ", ") + x;
        return out.empty() ? std::string("(none)") : out;
    };
    logf("aim sources: %s; button sources: %s", join(settings_.aimSources).c_str(),
         join(settings_.buttonSources).c_str());
    for (const auto& name : settings_.aimSources)
        if (auto* s = source(name); s && !s->providesAim()) logf("note: '%s' can't aim", name.c_str());
    for (const auto& name : settings_.buttonSources)
        if (auto* s = source(name); s && !s->providesButtons()) logf("note: '%s' has no buttons", name.c_str());
}

void Service::updateLaserMode() {
    // An invisible overlay with MakeOverlaysInteractiveIfVisible keeps SteamVR in laser
    // mode while it is shown (the technique Frametop's pointer helper uses).
    auto* ov = vr::VROverlay();
    if (laserMode_ == vr::k_ulOverlayHandleInvalid) {
        if (ov->CreateOverlay("frameeyepointer.lasermode", "FrameEyePointer laser mode", &laserMode_) !=
            vr::VROverlayError_None)
            return;
        std::vector<uint8_t> clear(4 * 4 * 4, 0);
        ov->SetOverlayRaw(laserMode_, clear.data(), 4, 4, 4);
        ov->SetOverlayWidthInMeters(laserMode_, 0.001f);
        ov->SetOverlayInputMethod(laserMode_, vr::VROverlayInputMethod_Mouse);
        ov->SetOverlayFlag(laserMode_, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);
        vr::HmdMatrix34_t below{};
        below.m[0][0] = below.m[1][1] = below.m[2][2] = 1;
        below.m[1][3] = -50; // far below the head, never hit
        ov->SetOverlayTransformTrackedDeviceRelative(laserMode_, vr::k_unTrackedDeviceIndex_Hmd, &below);
    }
    bool want = active_ && settings_.forceLaserMode;
    if (want == laserModeShown_) return;
    if (want) ov->ShowOverlay(laserMode_);
    else ov->HideOverlay(laserMode_);
    laserModeShown_ = want;
}

void Service::findOurDevice() {
    ours_ = vr::k_unTrackedDeviceIndexInvalid;
    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (sys_->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Invalid) continue;
        if (stringProp(i, vr::Prop_ControllerType_String) == "frameeyepointer") {
            ours_ = i;
            return;
        }
    }
}

bool Service::pollEvents() {
    vr::VREvent_t ev;
    while (sys_->PollNextEvent(&ev, sizeof ev)) {
        if (ev.eventType == vr::VREvent_Quit) {
            logf("SteamVR is quitting");
            sys_->AcknowledgeQuit_Exiting();
            return false;
        }
        if (ev.eventType == vr::VREvent_TrackedDeviceActivated) lastDeviceSearch_ = -1e9;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Per frame
// ---------------------------------------------------------------------------

frame::FrameContext Service::context(double t) {
    if (t - lastSlow_ > 0.5) {
        lastSlow_ = t;
        bool game = vr::VRApplications()->GetCurrentSceneProcessId() != 0;
        if (game != inGame_) logf(game ? "a game is running" : "no game running");
        inGame_ = game;
        auto level = sys_->GetTrackedDeviceActivityLevel(vr::k_unTrackedDeviceIndex_Hmd);
        worn_ = level == vr::k_EDeviceActivityLevel_UserInteraction ||
                level == vr::k_EDeviceActivityLevel_UserInteraction_Timeout;
    }
    frame::FrameContext ctx;
    ctx.time = t;
    ctx.settings = &settings_;
    ctx.enabled = enabled_;
    ctx.inGame = inGame_;
    ctx.dashboardVisible = vr::VROverlay()->IsDashboardVisible();
    ctx.worn = worn_;
    ctx.allowed = worn_ && (!inGame_ || (settings_.dashboardInGames && ctx.dashboardVisible));
    ctx.headValid = eyepointer::openvr::hmdPose(0.0f, ctx.headStanding);
    return ctx;
}

void Service::sendAim(const std::string& from, Aim aim, const frame::FrameContext& ctx) {
    if (from != "eyes" && settings_.smoothExternal) {
        auto& f = externalFilters_[from];
        f.setParams(settings_.filter);
        aim.direction = f.update(aim.direction, ctx.time);
    }
    const Vec3 &o = aim.origin, &d = aim.direction;
    if (aim.space == Aim::Space::Head) {
        driver_.send("gaze %.4f %.4f %.4f %.6f %.6f %.6f", o.x, o.y, o.z, d.x, d.y, d.z);
        return;
    }
    // Standing space -> the driver's raw tracking space, through the headset pose in both.
    Mat34 raw;
    if (!ctx.headValid || !eyepointer::openvr::hmdPose(0.0f, raw, vr::TrackingUniverseRawAndUncalibrated)) return;
    Vec3 ro = raw.transformPoint(ctx.headStanding.inverseTransformPoint(o));
    Vec3 rd = raw.rotate(ctx.headStanding.inverseRotate(d));
    driver_.send("ray %.4f %.4f %.4f %.6f %.6f %.6f", ro.x, ro.y, ro.z, rd.x, rd.y, rd.z);
}

void Service::setActive(bool active, double t) {
    if (active == active_) return;
    active_ = active;
    if (active) activeSince_ = t;
    driver_.send(active ? "show" : "hide");
    updateLaserMode();
    if (frame::g_verbose) logf("laser %s", active ? "connected" : "disconnected");
}

void Service::frame(double t) {
    if (t - lastConfigCheck_ > 1.0) {
        lastConfigCheck_ = t;
        reloadSettingsIfChanged();
    }
    if (ours_ == vr::k_unTrackedDeviceIndexInvalid && t - lastDeviceSearch_ > 2.0) {
        lastDeviceSearch_ = t;
        findOurDevice();
        if (ours_ != vr::k_unTrackedDeviceIndexInvalid) {
            logf("laser device is tracked device %u", ours_);
            warnedNoDriver_ = false;
        } else if (!warnedNoDriver_) {
            logf("the frameeyepointer SteamVR driver isn't loaded: run the installer, then restart SteamVR");
            warnedNoDriver_ = true;
        }
    }

    frame::FrameContext ctx = context(t);
    if (hub_) hub_->poll(t);

    // One UpdateActionState for every source's action sets.
    std::vector<vr::VRActiveActionSet_t> sets;
    for (auto& [name, s] : sources_) s->collectActionSets(ctx, sets);
    if (!sets.empty()) vr::VRInput()->UpdateActionState(sets.data(), sizeof sets[0], uint32_t(sets.size()));

    // Buttons from every button source, combined.
    eyepointer::ButtonState buttons;
    for (const auto& name : settings_.buttonSources)
        if (auto* s = source(name); s && s->providesButtons()) buttons.merge(s->readButtons(ctx));

    // Hold the toggle to switch the laser on and off.
    if (buttons.toggle) {
        if (toggleDownAt_ == 0) toggleDownAt_ = t;
        if (!toggleFired_ && t - toggleDownAt_ >= settings_.toggleHoldSeconds) {
            toggleFired_ = true;
            enabled_ = !enabled_;
            ctx.enabled = enabled_;
            logf("laser %s", enabled_ ? "on" : "off");
        }
    } else {
        toggleDownAt_ = 0;
        toggleFired_ = false;
    }

    // Aim: read every aim source (so filters stay current), use the first pointing one.
    std::map<std::string, Aim> aims;
    for (const auto& name : settings_.aimSources)
        if (auto* s = source(name); s && s->providesAim()) aims[name] = s->readAim(ctx);
    auto chosen = eyepointer::selectAim(settings_.aimSources, aims);
    if (chosen && *chosen != aimFrom_) logf("aiming with %s", chosen->c_str());
    aimFrom_ = chosen ? *chosen : "";

    bool haveDriver = ours_ != vr::k_unTrackedDeviceIndexInvalid;
    setActive(enabled_ && ctx.allowed && chosen.has_value() && haveDriver, t);
    if (active_) {
        sendAim(*chosen, aims[*chosen], ctx);
        ++aimFrames_;
    }

    eyepointer::ClickSequencer::Input in;
    in.time = t;
    in.active = active_;
    in.clickHeld = buttons.click;
    in.rightClickHeld = buttons.rightClick;
    in.wantClaim = active_ && t - activeSince_ > 0.4 && vr::VROverlay()->GetPrimaryDashboardDevice() != ours_;
    for (const auto& e : clicks_.update(in)) {
        const char* name = e.button == eyepointer::LaserButton::Click        ? "trigger"
                           : e.button == eyepointer::LaserButton::RightClick ? "b"
                                                                              : "a";
        driver_.send("btn %s %d", name, e.down ? 1 : 0);
        if (e.down && e.button != eyepointer::LaserButton::Claim) ++clicksSent_;
    }

    float scroll = active_ ? buttons.scroll : 0.0f;
    if (scroll != sentScroll_) {
        driver_.send("scroll 0 %.3f", scroll);
        sentScroll_ = scroll;
    }

    ++frames_;
    if (t - lastReport_ > 30.0) {
        std::string detail;
        for (auto& [name, s] : sources_)
            if (auto st = s->status(); !st.empty()) detail += "; " + st;
        logf("status: %s, %s, aimed %d of %d frames%s, %d clicks, laser held by device %u%s",
             enabled_ ? "on" : "off", active_ ? "connected" : "disconnected", aimFrames_, frames_,
             aimFrom_.empty() ? "" : (" with " + aimFrom_).c_str(), clicksSent_,
             vr::VROverlay()->GetPrimaryDashboardDevice(), (inGame_ ? " (game running)" + detail : detail).c_str());
        lastReport_ = t;
        frames_ = aimFrames_ = clicksSent_ = 0;
    }
}

int Service::run() {
    loadSettings();
    enabled_ = settings_.enabledAtStart;
    if (!connect()) return probe_ ? 2 : 0;
    logf("connected to SteamVR %s", sys_->GetRuntimeVersion());
    setupInput();
    buildSources();
    if (probe_) return probe();
    updateLaserMode();
    lastReport_ = now();

    while (!g_quit) {
        double t0 = now();
        bool synced = vr::VROverlay()->WaitFrameSync(30) == vr::VROverlayError_None;
        if (!pollEvents()) break;
        frame(now());
        if (!synced) {
            double spent = now() - t0;
            if (spent < 0.011) std::this_thread::sleep_for(std::chrono::duration<double>(0.011 - spent));
        }
    }
    eyepointer::ClickSequencer::Input off;
    off.time = now();
    for (const auto& e : clicks_.update(off)) (void)e;
    driver_.send("btn trigger 0");
    driver_.send("btn b 0");
    driver_.send("btn a 0");
    driver_.send("hide");
    vr::VR_Shutdown();
    logf("stopped");
    return 0;
}

// ---------------------------------------------------------------------------
// --probe: what this headset exposes, for setup and bug reports
// ---------------------------------------------------------------------------

int Service::probe() {
    logf("==== FrameEyePointer %s probe ====", FRAMEEYEPOINTER_VERSION);
    logf("system: %s", osRelease().c_str());
    logf("SteamVR runtime %s", sys_->GetRuntimeVersion());
    vr::ETrackedPropertyError perr = vr::TrackedProp_Success;
    bool eyes = sys_->GetBoolTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
                                                    vr::Prop_SupportsXrEyeGazeInteraction_Bool, &perr);
    logf("headset reports eye gaze support: %s",
         perr == vr::TrackedProp_Success ? (eyes ? "yes" : "no") : "unknown");
    findOurDevice();
    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        auto cls = sys_->GetTrackedDeviceClass(i);
        if (cls == vr::TrackedDeviceClass_Invalid) continue;
        logf("device %u: class %d, type '%s', role %d, connected %d%s", i, int(cls),
             stringProp(i, vr::Prop_ControllerType_String).c_str(),
             int(sys_->GetControllerRoleForTrackedDeviceIndex(i)), sys_->IsTrackedDeviceConnected(i) ? 1 : 0,
             i == ours_ ? "  <- laser device" : "");
    }
    logf("laser driver: %s", ours_ != vr::k_unTrackedDeviceIndexInvalid ? "loaded" : "NOT loaded");
    frame::FrameContext ctx = context(now());
    logf("game running: %s, dashboard open: %s, headset worn: %s, laser held by device %u",
         ctx.inGame ? "yes" : "no", ctx.dashboardVisible ? "yes" : "no", ctx.worn ? "yes" : "no",
         vr::VROverlay()->GetPrimaryDashboardDevice());
    for (auto& [name, s] : sources_) {
        logf("-- source %s --", name.c_str());
        s->probe(ctx);
    }
    logf("==== end of probe ====");
    vr::VR_Shutdown();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool probe = false;
    fs::path config = defaultConfigPath();
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--probe") probe = true;
        else if (a == "--verbose" || a == "-v") frame::g_verbose = true;
        else if (a == "--config" && i + 1 < argc) config = argv[++i];
        else if (a == "--version") {
            std::printf("frameeyepointerd %s\n", FRAMEEYEPOINTER_VERSION);
            return 0;
        } else {
            std::fprintf(stderr, "usage: frameeyepointerd [--probe] [--verbose] [--config FILE] [--version]\n");
            return a == "--help" || a == "-h" ? 0 : 1;
        }
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    logf("frameeyepointerd %s, settings %s", FRAMEEYEPOINTER_VERSION, config.c_str());
    Service service(config, probe);
    return service.run();
}
