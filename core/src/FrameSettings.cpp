#include "eyepointer/FrameSettings.h"

#include "eyepointer/IniFile.h"
#include "eyepointer/InputSources.h"

#include <cctype>
#include <set>

namespace eyepointer {

namespace {

const char* kDefaultIni = R"INI(; FrameEyePointer (Steam Frame) settings. Saved changes apply within a second.

[inputs]
; Where the laser points, in priority order: the first source that is pointing wins.
;   eyes  - the headset's eye tracking (built in)
;   any other name - an external source, such as a hand tracker, that sends rays
;   to the eye laser (see docs/INPUT_SOURCES.md). Example: aim_sources = hands, eyes
aim_sources = eyes
; What clicks, all combined. "controllers" (the Frame controllers) is built in;
; other names are external sources. Example: button_sources = controllers, hands
button_sources = controllers
; An external source that sends nothing for this long counts as gone.
external_timeout_ms = 250
; Smooth external rays with the [filter] settings (off if the source smooths itself).
smooth_external = false

[pointer]
; Turn the eye laser on when SteamVR starts (hold left View to toggle it).
enabled = true
; Where the gaze comes from:
;   auto   - SteamVR's eye tracking action outside games, the eye tracker's
;            shared memory during games
;   action - SteamVR's action only (the eye laser is off during games)
;   mmap   - the shared memory only (undocumented; may need an update after SteamVR updates)
gaze_source = auto
; Keep pointing at the SteamVR dashboard while a game runs.
dashboard_in_games = true
; Keep the last gaze through blinks and dropouts for this many seconds.
hold_gaze_seconds = 1.0
; Keep SteamVR in laser mode while the eye laser is on.
force_laser_mode = true
; Shared-memory source: both eyes' openness below this is a blink.
blink_openness = 0.15

[filter]
; One Euro filter on the eye direction, relative to the head.
; Lower min_cutoff_hz = steadier while you fixate; higher beta = less lag when the eyes move.
min_cutoff_hz = 0.8
beta = 0.04
derivative_cutoff_hz = 1.0
; Eye jumps larger than this (degrees) are taken immediately.
saccade_snap_deg = 6

[click]
; tap  - a press sends a whole click at once. Steam takes SteamVR out of laser
;        mode about 40 ms after any Frame controller press, so the click has to
;        land first. Reliable, but no dragging.
; hold - the click is held while the button is, so you can drag. Steam's switch
;        may cut drags short.
mode = tap
tap_ms = 25
; After a controller press or release, take the laser back this much later.
reclaim_delay_ms = 80
claim_hold_ms = 60
claim_interval_ms = 500
; Turn on SteamVR's "Enable global input from overlays" setting, so the bumpers
; reach the eye pointer while the dashboard or another app has input focus.
global_input = true
; Hold left View this long to turn the eye laser on or off.
toggle_hold_ms = 600
)INI";

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

const char* FrameSettings::defaultIni() { return kDefaultIni; }

FrameSettings FrameSettings::parse(const std::string& text, std::vector<std::string>* warnings) {
    FrameSettings s;
    IniFile ini = IniFile::parse(text, warnings);

    static const std::map<std::string, std::set<std::string>> known = {
        {"inputs", {"aim_sources", "button_sources", "external_timeout_ms", "smooth_external"}},
        {"pointer", {"enabled", "gaze_source", "dashboard_in_games", "hold_gaze_seconds",
                     "force_laser_mode", "blink_openness"}},
        {"filter", {"min_cutoff_hz", "beta", "derivative_cutoff_hz", "saccade_snap_deg"}},
        {"click", {"mode", "tap_ms", "reclaim_delay_ms", "claim_hold_ms", "claim_interval_ms",
                   "global_input", "toggle_hold_ms"}},
    };
    for (const auto& [section, keys] : ini.sections()) {
        auto k = known.find(section);
        if (k == known.end()) {
            if (warnings && !section.empty()) warnings->push_back("unknown section [" + section + "]");
            continue;
        }
        for (const auto& kv : keys)
            if (!k->second.count(kv.first) && warnings)
                warnings->push_back("unknown key '" + kv.first + "' in [" + section + "]");
    }

    if (ini.has("inputs", "aim_sources")) {
        auto list = splitList(ini.get("inputs", "aim_sources"));
        if (list.empty()) {
            if (warnings) warnings->push_back("[inputs] aim_sources is empty; using eyes");
        } else {
            s.aimSources = list;
        }
    }
    if (ini.has("inputs", "button_sources")) s.buttonSources = splitList(ini.get("inputs", "button_sources"));
    {
        int ms = static_cast<int>(s.externalTimeoutSeconds * 1000.0 + 0.5);
        ini.read("inputs", "external_timeout_ms", ms, warnings, 20, 5000);
        s.externalTimeoutSeconds = ms / 1000.0;
    }
    ini.read("inputs", "smooth_external", s.smoothExternal, warnings);

    ini.read("pointer", "enabled", s.enabledAtStart, warnings);
    if (ini.has("pointer", "gaze_source")) {
        std::string v = lower(ini.get("pointer", "gaze_source"));
        if (v == "auto") s.gazeSource = GazeSource::Auto;
        else if (v == "action") s.gazeSource = GazeSource::Action;
        else if (v == "mmap") s.gazeSource = GazeSource::Mmap;
        else if (warnings) warnings->push_back("[pointer] gaze_source must be auto, action or mmap");
    }
    ini.read("pointer", "dashboard_in_games", s.dashboardInGames, warnings);
    ini.read("pointer", "hold_gaze_seconds", s.holdGazeSeconds, warnings, 0.0, 10.0);
    ini.read("pointer", "force_laser_mode", s.forceLaserMode, warnings);
    ini.read("pointer", "blink_openness", s.blinkOpenness, warnings, 0.0f, 1.0f);

    ini.read("filter", "min_cutoff_hz", s.filter.minCutoffHz, warnings, 0.01f, 100.0f);
    ini.read("filter", "beta", s.filter.beta, warnings, 0.0f, 10.0f);
    ini.read("filter", "derivative_cutoff_hz", s.filter.derivativeCutoffHz, warnings, 0.01f, 100.0f);
    ini.read("filter", "saccade_snap_deg", s.filter.saccadeSnapDeg, warnings, 0.1f, 180.0f);

    if (ini.has("click", "mode")) {
        std::string v = lower(ini.get("click", "mode"));
        if (v == "tap") s.click.mode = ClickTiming::Mode::Tap;
        else if (v == "hold") s.click.mode = ClickTiming::Mode::Hold;
        else if (warnings) warnings->push_back("[click] mode must be tap or hold");
    }
    auto ms = [&](const char* key, double& seconds, int min, int max) {
        int v = static_cast<int>(seconds * 1000.0 + 0.5);
        ini.read("click", key, v, warnings, min, max);
        seconds = v / 1000.0;
    };
    ms("tap_ms", s.click.tapSeconds, 1, 500);
    ms("reclaim_delay_ms", s.click.reclaimDelaySeconds, 0, 2000);
    ms("claim_hold_ms", s.click.claimHoldSeconds, 10, 1000);
    ms("claim_interval_ms", s.click.claimIntervalSeconds, 50, 10000);
    ini.read("click", "global_input", s.globalInput, warnings);
    ms("toggle_hold_ms", s.toggleHoldSeconds, 100, 5000);
    return s;
}

} // namespace eyepointer
