#include "TestHarness.h"

#include "eyepointer/DesktopMapping.h"
#include "eyepointer/PointerController.h"

using namespace eyepointer;

namespace {

// 1080p primary on the left, 1440p on the right raised by 360 px.
std::vector<Monitor> twoMonitors() {
    return {{{0, 0, 1920, 1080}, true}, {{1920, -360, 2560, 1440}, false}};
}

PointerInput at(double t, int x, int y) {
    PointerInput in;
    in.timeSeconds = t;
    in.gazePixel = Point{x, y};
    return in;
}

int count(const std::vector<MouseCommand>& cmds, MouseCommand::Type type) {
    int n = 0;
    for (const auto& c : cmds) n += c.type == type;
    return n;
}

} // namespace

TEST("desktop: selector parsing") {
    CHECK(DesktopSelector::parse("auto")->kind == DesktopSelector::Kind::Auto);
    CHECK(DesktopSelector::parse("virtual")->kind == DesktopSelector::Kind::VirtualScreen);
    CHECK(DesktopSelector::parse("primary")->kind == DesktopSelector::Kind::Primary);
    auto m = DesktopSelector::parse("monitor:2");
    CHECK(m && m->kind == DesktopSelector::Kind::MonitorIndex && m->monitorIndex == 2);
    auto r = DesktopSelector::parse("rect:-1920,0,1920,1080");
    CHECK(r && r->kind == DesktopSelector::Kind::Explicit);
    CHECK(r->rect == (Rect{-1920, 0, 1920, 1080}));
    CHECK(!DesktopSelector::parse("monitor:x"));
    CHECK(!DesktopSelector::parse("rect:0,0,0,10"));
    CHECK(!DesktopSelector::parse("rect:1,2,3,4junk"));
    CHECK(!DesktopSelector::parse("everything"));
}

TEST("desktop: virtual screen spans all monitors") {
    CHECK(virtualScreen(twoMonitors()) == (Rect{0, -360, 4480, 1440}));
    CHECK(virtualScreen({}) == Rect{});
}

TEST("desktop: auto picks by texture size") {
    auto mons = twoMonitors();
    DesktopSelector a;
    CHECK(*chooseDesktopRect(a, 4480, 1440, mons) == (Rect{0, -360, 4480, 1440}));
    CHECK(*chooseDesktopRect(a, 1920, 1080, mons) == (Rect{0, 0, 1920, 1080}));
    CHECK(*chooseDesktopRect(a, 2560, 1440, mons) == (Rect{1920, -360, 2560, 1440}));
    // Downscaled whole-desktop capture keeps the aspect ratio.
    CHECK(*chooseDesktopRect(a, 2240, 720, mons) == (Rect{0, -360, 4480, 1440}));
    // A browser window or similar: not a desktop.
    CHECK(!chooseDesktopRect(a, 800, 800, mons));
    CHECK(!chooseDesktopRect(a, 1920, 1080, {}));
}

TEST("desktop: explicit selectors") {
    auto mons = twoMonitors();
    DesktopSelector s;
    s.kind = DesktopSelector::Kind::MonitorIndex;
    s.monitorIndex = 1;
    CHECK(*chooseDesktopRect(s, 0, 0, mons) == mons[1].rect);
    s.monitorIndex = 5;
    CHECK(!chooseDesktopRect(s, 0, 0, mons));
    s.kind = DesktopSelector::Kind::Primary;
    CHECK(*chooseDesktopRect(s, 0, 0, mons) == mons[0].rect);
}

TEST("desktop: uv to pixel uses v-up and stays in bounds") {
    auto mons = twoMonitors();
    Rect primary{0, 0, 1920, 1080};
    CHECK(*uvToDesktopPixel(0, 1, primary, mons) == (Point{0, 0}));
    CHECK(*uvToDesktopPixel(1, 0, primary, mons) == (Point{1919, 1079}));
    CHECK(*uvToDesktopPixel(0.5f, 0.5f, primary, mons) == (Point{960, 540}));
    CHECK(!uvToDesktopPixel(1.2f, 0.5f, primary, mons));
    CHECK(!uvToDesktopPixel(0.5f, -0.1f, primary, mons));
}

TEST("desktop: gaps between monitors are not mapped") {
    auto mons = twoMonitors();
    Rect all = virtualScreen(mons);
    // Top-left of the virtual screen is above the 1080p monitor: a gap.
    CHECK(!uvToDesktopPixel(0.01f, 0.99f, all, mons));
    auto p = uvToDesktopPixel(0.9f, 0.5f, all, mons);
    CHECK(p && mons[1].rect.contains(p->x, p->y));
}

TEST("pointer: cursor follows gaze outside the deadband only") {
    PointerController pc;
    auto out = pc.update(at(0.00, 100, 100));
    CHECK(count(out, MouseCommand::Type::Move) == 1);
    out = pc.update(at(0.01, 103, 102)); // jitter
    CHECK(out.empty());
    out = pc.update(at(0.02, 130, 100));
    CHECK(count(out, MouseCommand::Type::Move) == 1);
    CHECK(out[0].position == (Point{130, 100}));
}

TEST("pointer: click lands on the steady cursor") {
    PointerController pc;
    pc.update(at(0.00, 500, 500));
    PointerInput in = at(0.01, 503, 498); // within deadband
    in.leftHeld = true;
    auto out = pc.update(in);
    CHECK(out.size() == 1);
    CHECK(out[0].type == MouseCommand::Type::ButtonDown);
    CHECK(out[0].button == MouseButton::Left);
    in = at(0.02, 520, 500); // eye drifts below drag threshold: cursor stays put
    in.leftHeld = true;
    CHECK(pc.update(in).empty());
    // Release happens where the click was, then the cursor resumes following the gaze.
    in = at(0.03, 520, 500);
    out = pc.update(in);
    CHECK(out.size() == 2);
    CHECK(out[0].type == MouseCommand::Type::ButtonUp);
    CHECK(out[1].type == MouseCommand::Type::Move && out[1].position == (Point{520, 500}));
}

TEST("pointer: presses while looking away are ignored") {
    PointerController pc;
    PointerInput in;
    in.timeSeconds = 0;
    in.leftHeld = true;
    CHECK(pc.update(in).empty());
    // Looking at the desktop while still holding: no phantom press.
    in = at(0.01, 10, 10);
    in.leftHeld = true;
    auto out = pc.update(in);
    CHECK(count(out, MouseCommand::Type::ButtonDown) == 0);
    in.leftHeld = false;
    out = pc.update(in);
    CHECK(count(out, MouseCommand::Type::ButtonUp) == 0);
}

TEST("pointer: holding and looking far away drags") {
    PointerController pc;
    PointerInput in = at(0, 100, 100);
    in.leftHeld = true;
    auto out = pc.update(in);
    CHECK(count(out, MouseCommand::Type::ButtonDown) == 1);
    in = at(0.01, 200, 100);
    in.leftHeld = true;
    out = pc.update(in);
    CHECK(count(out, MouseCommand::Type::Move) == 1);
    CHECK(pc.buttonHeld(MouseButton::Left));
}

TEST("pointer: right click and disable releases buttons") {
    PointerController pc;
    PointerInput in = at(0, 50, 50);
    in.rightHeld = true;
    auto out = pc.update(in);
    CHECK(count(out, MouseCommand::Type::ButtonDown) == 1);
    CHECK(out.back().button == MouseButton::Right);
    in.enabled = false;
    out = pc.update(in);
    CHECK(out.size() == 1 && out[0].type == MouseCommand::Type::ButtonUp);
    // Re-enabling with the button still held must not press again.
    in.enabled = true;
    out = pc.update(in);
    CHECK(count(out, MouseCommand::Type::ButtonDown) == 0);
}

TEST("pointer: thumbstick scrolls at the configured rate") {
    PointerController pc;
    int total = 0;
    for (int i = 0; i <= 90; ++i) {
        PointerInput in = at(i / 90.0, 300, 300);
        in.scroll = 1.0f;
        for (const auto& c : pc.update(in))
            if (c.type == MouseCommand::Type::Wheel) total += c.wheelDelta;
    }
    CHECK(total >= 11 * 120 && total <= 12 * 120);
    // Inside the deadzone nothing happens.
    PointerInput in = at(2.0, 300, 300);
    in.scroll = 0.2f;
    CHECK(count(pc.update(in), MouseCommand::Type::Wheel) == 0);
}

TEST("pointer: looking away then back moves the cursor again") {
    PointerController pc;
    pc.update(at(0, 100, 100));
    PointerInput away;
    away.timeSeconds = 0.01;
    CHECK(pc.update(away).empty());
    auto out = pc.update(at(0.02, 100, 100));
    CHECK(count(out, MouseCommand::Type::Move) == 1);
}
