#include "eyepointer/Settings.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace eyepointer {

namespace {

const char* kDefaultIni = R"INI(; FrameEyePointer settings. Saved changes are picked up while the app runs.

[pointer]
; Show the pointer when the app starts (toggle with the left View button).
enabled = true
; Reticle size in degrees of your field of view, colour (RRGGBB) and opacity.
reticle_degrees = 1.0
reticle_color = 00E5FF
reticle_alpha = 0.9
; Show the reticle in free space when you are not looking at a target overlay.
reticle_when_no_target = true
; Free-space depth: "fixation" uses the eye tracker's focus distance, or a number in metres.
free_depth = fixation
min_depth_m = 0.3
max_depth_m = 10
; Short controller pulse when a click lands on a desktop.
haptic_on_click = true

[filter]
; One Euro filter on eye direction (applied relative to your head).
; Lower min_cutoff_hz = steadier but laggier while fixating.
min_cutoff_hz = 0.8
; Higher beta = less lag when your eyes move.
beta = 0.04
derivative_cutoff_hz = 1.0
; Eye jumps larger than this (degrees) are taken immediately.
saccade_snap_deg = 6

[mouse]
; Drive the Windows mouse when you look at a desktop overlay.
enabled = true
; Cursor only moves when the gaze point moves this many pixels.
deadband_px = 6
; While a button is held, the gaze must move this far before it becomes a drag.
drag_threshold_px = 40
scroll_deadzone = 0.3
scroll_notches_per_second = 12

; ---------------------------------------------------------------------------
; Target overlays. key = the OpenVR overlay key (run FrameEyePointer --probe
; to list the ones on your system; a trailing * matches key0..key31).
; mode = desktop  -> the overlay shows your Windows desktop, gaze moves the mouse
; mode = pointer  -> only show the reticle on it
; desktop = auto | virtual | primary | monitor:N | rect:x,y,w,h
; ---------------------------------------------------------------------------

[target.desktopplus]
key = elvissteinjr.DesktopPlus*
mode = desktop
desktop = auto

[target.dashboard]
key = system.vrdashboard
mode = pointer
)INI";

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool parseBool(const std::string& v, bool& out) {
    std::string l = lower(v);
    if (l == "true" || l == "yes" || l == "on" || l == "1") {
        out = true;
        return true;
    }
    if (l == "false" || l == "no" || l == "off" || l == "0") {
        out = false;
        return true;
    }
    return false;
}

bool parseFloat(const std::string& v, float& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    float f = std::strtof(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0' || !(f == f)) return false;
    out = f;
    return true;
}

bool parseInt(const std::string& v, int& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0') return false;
    out = static_cast<int>(n);
    return true;
}

bool parseColor(const std::string& v, uint32_t& out) {
    std::string s = v;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.size() != 6) return false;
    char* end = nullptr;
    unsigned long n = std::strtoul(s.c_str(), &end, 16);
    if (*end != '\0') return false;
    out = static_cast<uint32_t>(n);
    return true;
}

struct Parser {
    Settings& s;
    std::vector<std::string>* warnings;
    int line = 0;

    void warn(const std::string& msg) {
        if (warnings) warnings->push_back("line " + std::to_string(line) + ": " + msg);
    }

    template <typename T, typename F>
    void set(const std::string& key, const std::string& value, T& field, F parser) {
        T tmp{};
        if (parser(value, tmp))
            field = tmp;
        else
            warn("bad value '" + value + "' for " + key);
    }

    void positive(const std::string& key, const std::string& value, float& field) {
        float tmp = 0;
        if (parseFloat(value, tmp) && tmp > 0)
            field = tmp;
        else
            warn("'" + key + "' must be a positive number");
    }

    void pointer(const std::string& k, const std::string& v) {
        if (k == "enabled") set(k, v, s.enabledAtStart, parseBool);
        else if (k == "reticle_degrees") positive(k, v, s.reticleDegrees);
        else if (k == "reticle_color") set(k, v, s.reticleColor, parseColor);
        else if (k == "reticle_alpha") set(k, v, s.reticleAlpha, parseFloat);
        else if (k == "reticle_when_no_target") set(k, v, s.reticleWhenNoTarget, parseBool);
        else if (k == "free_depth") {
            if (lower(v) == "fixation") {
                s.freeDepthFromFixation = true;
            } else {
                float m = 0;
                if (parseFloat(v, m) && m > 0) {
                    s.freeDepthFromFixation = false;
                    s.freeDepthMeters = m;
                } else {
                    warn("free_depth must be 'fixation' or a distance in metres");
                }
            }
        } else if (k == "min_depth_m") positive(k, v, s.minDepthMeters);
        else if (k == "max_depth_m") positive(k, v, s.maxDepthMeters);
        else if (k == "haptic_on_click") set(k, v, s.hapticOnClick, parseBool);
        else warn("unknown key '" + k + "' in [pointer]");
    }

    void filter(const std::string& k, const std::string& v) {
        GazeFilterParams& f = s.filter;
        if (k == "min_cutoff_hz") positive(k, v, f.minCutoffHz);
        else if (k == "beta") set(k, v, f.beta, parseFloat);
        else if (k == "derivative_cutoff_hz") positive(k, v, f.derivativeCutoffHz);
        else if (k == "saccade_snap_deg") positive(k, v, f.saccadeSnapDeg);
        else if (k == "max_gap_seconds") positive(k, v, f.maxGapSeconds);
        else warn("unknown key '" + k + "' in [filter]");
    }

    void mouse(const std::string& k, const std::string& v) {
        PointerParams& p = s.pointer;
        if (k == "enabled") set(k, v, s.mouseEnabled, parseBool);
        else if (k == "deadband_px") set(k, v, p.deadbandPx, parseInt);
        else if (k == "drag_threshold_px") set(k, v, p.dragThresholdPx, parseInt);
        else if (k == "scroll_deadzone") set(k, v, p.scrollDeadzone, parseFloat);
        else if (k == "scroll_notches_per_second") set(k, v, p.scrollNotchesPerSecond, parseFloat);
        else warn("unknown key '" + k + "' in [mouse]");
    }

    void target(TargetConfig& t, const std::string& k, const std::string& v) {
        if (k == "key") {
            t.key = v;
        } else if (k == "mode") {
            std::string m = lower(v);
            if (m == "desktop") t.mode = TargetConfig::Mode::Desktop;
            else if (m == "pointer") t.mode = TargetConfig::Mode::Pointer;
            else warn("mode must be 'desktop' or 'pointer'");
        } else if (k == "desktop") {
            if (auto sel = DesktopSelector::parse(lower(v))) t.desktop = *sel;
            else warn("desktop must be auto, virtual, primary, monitor:N or rect:x,y,w,h");
        } else {
            warn("unknown key '" + k + "' in [target." + t.name + "]");
        }
    }
};

} // namespace

const char* Settings::defaultIni() { return kDefaultIni; }

Settings Settings::parse(const std::string& text, std::vector<std::string>* warnings) {
    Settings s;
    Parser p{s, warnings};
    std::vector<TargetConfig> targets;
    std::string section;
    TargetConfig* current = nullptr;

    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        ++p.line;
        std::string l = trim(raw);
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l.front() == '[') {
            if (l.back() != ']') {
                p.warn("malformed section header");
                section.clear();
                continue;
            }
            section = lower(trim(l.substr(1, l.size() - 2)));
            current = nullptr;
            if (section.rfind("target.", 0) == 0) {
                targets.push_back({});
                targets.back().name = section.substr(7);
                current = &targets.back();
            } else if (section != "pointer" && section != "filter" && section != "mouse") {
                p.warn("unknown section [" + section + "]");
            }
            continue;
        }
        size_t eq = l.find('=');
        if (eq == std::string::npos) {
            p.warn("expected key = value");
            continue;
        }
        std::string key = lower(trim(l.substr(0, eq)));
        std::string value = trim(l.substr(eq + 1));
        // Allow trailing comments after whitespace.
        size_t c = value.find(" ;");
        if (c != std::string::npos) value = trim(value.substr(0, c));

        if (section == "pointer") p.pointer(key, value);
        else if (section == "filter") p.filter(key, value);
        else if (section == "mouse") p.mouse(key, value);
        else if (current) p.target(*current, key, value);
    }

    if (s.minDepthMeters > s.maxDepthMeters) {
        p.line = 0;
        p.warn("min_depth_m is larger than max_depth_m; swapping them");
        std::swap(s.minDepthMeters, s.maxDepthMeters);
    }

    for (auto& t : targets) {
        if (t.key.empty()) {
            if (warnings) warnings->push_back("[target." + t.name + "] has no key; ignored");
            continue;
        }
        s.targets.push_back(t);
    }
    if (targets.empty()) {
        // No target sections at all: fall back to the defaults.
        Settings d = parse(kDefaultIni);
        s.targets = d.targets;
    }
    return s;
}

} // namespace eyepointer
