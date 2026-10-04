#include "TestHarness.h"

#include "eyepointer/ClickSequencer.h"
#include "eyepointer/EyeServer.h"
#include "eyepointer/FrameSettings.h"
#include "eyepointer/IniFile.h"
#include "eyepointer/InputSources.h"

#include <cstring>
#include <vector>

using namespace eyepointer;

namespace {

// Builds a synthetic eye-server.mmap image for a layout version.
std::vector<uint8_t> eyeFile(uint32_t version, Vec3 fixation, float openL = 0.7f,
                             float openR = 0.7f, uint32_t initialized = 1) {
    const auto* layout = eyeserver::findLayout(version);
    size_t size = layout ? layout->minSize : 0x4f21f;
    size_t rec = layout ? layout->recordOffset : 0x157;
    std::vector<uint8_t> f(size, 0);
    auto put = [&](size_t off, const void* p, size_t n) { std::memcpy(&f[off], p, n); };
    put(eyeserver::kVersionOffset, &version, 4);
    put(eyeserver::kInitializedOffset, &initialized, 4);
    uint32_t counter = 1234;
    put(eyeserver::kCounterOffset, &counter, 4);
    uint32_t producing = 1;
    put(rec + eyeserver::kProducerState, &producing, 4);
    double t = 42.5;
    put(rec + eyeserver::kSampleTime, &t, 8);
    float eyes[6] = {0.1f, 0, -0.995f, -0.1f, 0, -0.995f};
    put(rec + eyeserver::kGaze, eyes, sizeof eyes);
    float fix[3] = {fixation.x, fixation.y, fixation.z};
    put(rec + eyeserver::kFixation, fix, sizeof fix);
    float open[2] = {openL, openR};
    put(rec + eyeserver::kOpenness, open, sizeof open);
    return f;
}

using E = LaserButtonEvent;
using B = LaserButton;

ClickSequencer::Input at(double t, bool click = false, bool right = false, bool active = true,
                         bool wantClaim = false) {
    ClickSequencer::Input in;
    in.time = t;
    in.active = active;
    in.clickHeld = click;
    in.rightClickHeld = right;
    in.wantClaim = wantClaim;
    return in;
}

} // namespace

TEST("eyeserver: parses known layouts") {
    for (uint32_t v : {4u, 5u}) {
        auto f = eyeFile(v, {0.0f, 0.2f, -1.0f});
        EyeServerSample s;
        CHECK(parseEyeServer(f.data(), f.size(), s) == EyeServerStatus::Ok);
        CHECK(s.version == v);
        CHECK(s.counter == 1234);
        CHECK(s.producing);
        CHECK_NEAR(s.sampleTime, 42.5, 1e-9);
        CHECK_NEAR(s.left.x, 0.1, 1e-6);
        CHECK_NEAR(s.right.x, -0.1, 1e-6);
        Vec3 d = s.direction();
        CHECK_NEAR(length(d), 1.0, 1e-5);
        CHECK(d.y > 0.15f && d.z < -0.9f);
        CHECK(!s.blinking(0.15f));
    }
}

TEST("eyeserver: refuses what it doesn't know") {
    EyeServerSample s;
    auto unknown = eyeFile(9, {0, 0, -1});
    CHECK(parseEyeServer(unknown.data(), unknown.size(), s) == EyeServerStatus::UnknownVersion);
    CHECK(s.version == 9);
    auto uninit = eyeFile(5, {0, 0, -1}, 0.7f, 0.7f, 0);
    CHECK(parseEyeServer(uninit.data(), uninit.size(), s) == EyeServerStatus::NotInitialized);
    auto small = eyeFile(5, {0, 0, -1});
    CHECK(parseEyeServer(small.data(), 0x1000, s) == EyeServerStatus::TooSmall);
    CHECK(parseEyeServer(small.data(), 8, s) == EyeServerStatus::TooSmall);
    CHECK(eyeserver::knownVersions() == "4, 5");
}

TEST("eyeserver: blinks and degenerate fixation") {
    EyeServerSample s;
    auto closed = eyeFile(5, {0, 0, -1}, 0.05f, 0.1f);
    CHECK(parseEyeServer(closed.data(), closed.size(), s) == EyeServerStatus::Ok);
    CHECK(s.blinking(0.15f));
    // Fixation at the origin: fall back to the mean of the two eyes (straight ahead).
    auto zero = eyeFile(5, {0, 0, 0});
    CHECK(parseEyeServer(zero.data(), zero.size(), s) == EyeServerStatus::Ok);
    Vec3 d = s.direction();
    CHECK_NEAR(d.x, 0.0, 1e-5);
    CHECK_NEAR(d.z, -1.0, 1e-5);
    CHECK(!dumpEyeServer(zero.data(), zero.size()).empty());
}

TEST("click: tap sends a whole click, then reclaims the laser") {
    ClickSequencer cs;
    CHECK(cs.update(at(0.000)).empty());
    auto out = cs.update(at(1.000, true));
    CHECK(out.size() == 1 && out[0] == (E{B::Click, true}));
    CHECK(cs.update(at(1.011, true)).empty());
    out = cs.update(at(1.030, true)); // tap_ms 25 has passed
    CHECK(out.size() == 1 && out[0] == (E{B::Click, false}));
    out = cs.update(at(1.090, true)); // reclaim at +80 ms
    CHECK(out.size() == 1 && out[0] == (E{B::Claim, true}));
    out = cs.update(at(1.150, true));
    CHECK(out.size() == 1 && out[0] == (E{B::Claim, false}));
    // Release: another reclaim after Steam's switch.
    CHECK(cs.update(at(1.200, false)).empty());
    out = cs.update(at(1.290, false));
    CHECK(out.size() == 1 && out[0] == (E{B::Claim, true}));
}

TEST("click: a click finishes even if frames are slow") {
    ClickSequencer cs;
    cs.update(at(0));
    cs.update(at(1.0, true));
    auto out = cs.update(at(1.2, true)); // one late frame covers the up and the whole claim
    CHECK(out.size() == 3);
    CHECK(out[0] == (E{B::Click, false}));
    CHECK(out[1] == (E{B::Claim, true}));
    CHECK(out[2] == (E{B::Claim, false}));
}

TEST("click: hold mode follows the button") {
    ClickTiming t;
    t.mode = ClickTiming::Mode::Hold;
    ClickSequencer cs(t);
    cs.update(at(0));
    auto out = cs.update(at(1.0, false, true));
    CHECK(out.size() == 1 && out[0] == (E{B::RightClick, true}));
    CHECK(cs.update(at(1.5, false, true)).empty());
    out = cs.update(at(2.0, false, false));
    CHECK(out.size() == 1 && out[0] == (E{B::RightClick, false}));
}

TEST("click: going inactive lets go of everything") {
    ClickTiming t;
    t.mode = ClickTiming::Mode::Hold;
    ClickSequencer cs(t);
    cs.update(at(0));
    cs.update(at(1.0, true));
    auto out = cs.update(at(1.1, true, false, false));
    CHECK(out.size() == 1 && out[0] == (E{B::Click, false}));
    // Becoming active with the button still held does not press.
    CHECK(cs.update(at(1.2, true)).empty());
    // A press while inactive does nothing.
    cs.update(at(1.3, false, false, false));
    CHECK(cs.update(at(1.4, true, false, false)).empty());
}

TEST("click: claims are rate limited and wait for buttons") {
    ClickSequencer cs;
    auto out = cs.update(at(0.0, false, false, true, true));
    CHECK(out.size() == 1 && out[0] == (E{B::Claim, true}));
    out = cs.update(at(0.1, false, false, true, true));
    CHECK(out.size() == 1 && out[0] == (E{B::Claim, false}));
    CHECK(cs.update(at(0.3, false, false, true, true)).empty()); // inside the interval
    out = cs.update(at(0.6, false, false, true, true));
    CHECK(out.size() == 1 && out[0] == (E{B::Claim, true}));
    cs.update(at(0.7, false, false, true, false));
    // While a button is held, no claim even if wanted (it would break the press).
    ClickTiming t;
    t.mode = ClickTiming::Mode::Hold;
    ClickSequencer hold(t);
    hold.update(at(0));
    hold.update(at(1.0, true));
    CHECK(hold.update(at(5.0, true, false, true, true)).empty());
}

TEST("ini: sections, comments and typed reads") {
    std::vector<std::string> w;
    IniFile ini = IniFile::parse("[A]\nx = 1 ; note\ny=yes\nz = 2.5 # c\nbad = 1x\n", &w);
    CHECK(w.empty());
    int x = 0;
    bool y = false;
    double z = 0;
    ini.read("a", "x", x, &w);
    ini.read("a", "y", y, &w);
    ini.read("a", "z", z, &w);
    CHECK(x == 1 && y && z == 2.5);
    int bad = 7;
    ini.read("a", "bad", bad, &w);
    CHECK(bad == 7 && w.size() == 1);
    int range = 3;
    IniFile::parse("[a]\nr = 99\n").read("a", "r", range, &w, 0, 10);
    CHECK(range == 3 && w.size() == 2);
}

TEST("frame settings: defaults file parses cleanly") {
    std::vector<std::string> w;
    FrameSettings s = FrameSettings::parse(FrameSettings::defaultIni(), &w);
    for (const auto& m : w) std::printf("    warning: %s\n", m.c_str());
    CHECK(w.empty());
    FrameSettings d;
    CHECK(s.enabledAtStart == d.enabledAtStart);
    CHECK(s.gazeSource == FrameSettings::GazeSource::Auto);
    CHECK(s.click.mode == ClickTiming::Mode::Tap);
    CHECK_NEAR(s.click.tapSeconds, d.click.tapSeconds, 1e-9);
    CHECK_NEAR(s.click.reclaimDelaySeconds, d.click.reclaimDelaySeconds, 1e-9);
    CHECK_NEAR(s.click.claimIntervalSeconds, d.click.claimIntervalSeconds, 1e-9);
    CHECK_NEAR(s.toggleHoldSeconds, d.toggleHoldSeconds, 1e-9);
    CHECK_NEAR(s.filter.beta, d.filter.beta, 1e-6);
    CHECK(s.globalInput);
}

TEST("frame settings: overrides and bad values") {
    std::vector<std::string> w;
    FrameSettings s = FrameSettings::parse(
        "[pointer]\ngaze_source = mmap\nblink_openness = 3\n[click]\nmode = hold\ntap_ms = 40\n"
        "frobnicate = 1\n[extra]\n",
        &w);
    CHECK(s.gazeSource == FrameSettings::GazeSource::Mmap);
    CHECK(s.click.mode == ClickTiming::Mode::Hold);
    CHECK_NEAR(s.click.tapSeconds, 0.040, 1e-9);
    CHECK_NEAR(s.blinkOpenness, FrameSettings{}.blinkOpenness, 1e-6);
    CHECK(w.size() == 3); // blink_openness range, unknown key, unknown section
}

TEST("inputs: protocol messages") {
    auto a = parseInputMessage("aim hands tracking 0.1 1.2 -0.3 0 0 -2");
    CHECK(a && a->type == InputMessage::Type::Aim && a->source == "hands");
    CHECK(a->aim.valid && a->aim.space == Aim::Space::Tracking);
    CHECK_NEAR(a->aim.direction.z, -1.0, 1e-6); // normalized
    CHECK_NEAR(a->aim.origin.y, 1.2, 1e-6);
    auto none = parseInputMessage("aim Hands none");
    CHECK(none && none->source == "hands" && !none->aim.valid);
    auto b = parseInputMessage("btn hands click 1");
    CHECK(b && b->type == InputMessage::Type::Button && b->button == "click" && b->down);
    auto s = parseInputMessage("scroll hands -3");
    CHECK(s && s->type == InputMessage::Type::Scroll && s->scroll == -1.0f);
    CHECK(!parseInputMessage("aim hands head 0 0 0 0 0 0"));     // no direction
    CHECK(!parseInputMessage("aim hands world 0 0 0 0 0 -1"));   // bad space
    CHECK(!parseInputMessage("aim hands head 0 0 0 0 0"));       // missing number
    CHECK(!parseInputMessage("aim hands head 0 0 0 0 0 -1 9"));  // extra
    CHECK(!parseInputMessage("btn hands jump 1"));
    CHECK(!parseInputMessage("btn hands click 2"));
    CHECK(!parseInputMessage("btn h@nds click 1"));
    CHECK(!parseInputMessage("teleport hands"));
    CHECK(!parseInputMessage(""));
}

TEST("inputs: first pointing source wins, buttons combine") {
    std::map<std::string, Aim> aims;
    Aim eye;
    eye.valid = true;
    aims["eyes"] = eye;
    aims["hands"] = Aim{};
    CHECK(*selectAim({"hands", "eyes"}, aims) == "eyes");
    aims["hands"].valid = true;
    CHECK(*selectAim({"hands", "eyes"}, aims) == "hands");
    CHECK(!selectAim({"feet"}, aims));
    ButtonState x, y;
    x.click = true;
    x.scroll = 0.2f;
    y.rightClick = true;
    y.scroll = -0.7f;
    x.merge(y);
    CHECK(x.click && x.rightClick && !x.toggle);
    CHECK_NEAR(x.scroll, -0.7, 1e-6);
    auto list = splitList(" Hands,eyes  ,, controllers ");
    CHECK(list.size() == 3 && list[0] == "hands" && list[2] == "controllers");
}

TEST("frame settings: input source lists") {
    std::vector<std::string> w;
    FrameSettings s = FrameSettings::parse(
        "[inputs]\naim_sources = hands, eyes\nbutton_sources = controllers hands\nexternal_timeout_ms = 500\n", &w);
    CHECK(w.empty());
    CHECK(s.aimSources.size() == 2 && s.aimSources[0] == "hands");
    CHECK(s.buttonSources.size() == 2 && s.buttonSources[1] == "hands");
    CHECK_NEAR(s.externalTimeoutSeconds, 0.5, 1e-9);
    FrameSettings d = FrameSettings::parse("[inputs]\naim_sources =\n", &w);
    CHECK(d.aimSources.size() == 1 && d.aimSources[0] == "eyes" && w.size() == 1);
}
