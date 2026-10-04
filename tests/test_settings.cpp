#include "TestHarness.h"

#include "eyepointer/Settings.h"

using namespace eyepointer;

TEST("settings: default file parses cleanly to the defaults") {
    std::vector<std::string> warnings;
    Settings s = Settings::parse(Settings::defaultIni(), &warnings);
    for (const auto& w : warnings) std::printf("    warning: %s\n", w.c_str());
    CHECK(warnings.empty());
    Settings d;
    CHECK(s.enabledAtStart == d.enabledAtStart);
    CHECK_NEAR(s.reticleDegrees, d.reticleDegrees, 1e-6);
    CHECK(s.reticleColor == d.reticleColor);
    CHECK(s.freeDepthFromFixation);
    CHECK_NEAR(s.filter.minCutoffHz, d.filter.minCutoffHz, 1e-6);
    CHECK_NEAR(s.filter.beta, d.filter.beta, 1e-6);
    CHECK(s.pointer.deadbandPx == d.pointer.deadbandPx);
    CHECK(s.pointer.dragThresholdPx == d.pointer.dragThresholdPx);
    CHECK(s.targets.size() == 2);
    CHECK(s.targets[0].key == "elvissteinjr.DesktopPlus*");
    CHECK(s.targets[0].mode == TargetConfig::Mode::Desktop);
    CHECK(s.targets[1].key == "system.vrdashboard");
    CHECK(s.targets[1].mode == TargetConfig::Mode::Pointer);
}

TEST("settings: values, comments and targets") {
    const char* ini = R"(
; comment
[pointer]
enabled = off
reticle_color = #FF8800
free_depth = 1.5   ; metres
[filter]
beta = 0.1
[mouse]
deadband_px = 10
[target.mine]
key = my.overlay
mode = desktop
desktop = monitor:1
)";
    std::vector<std::string> warnings;
    Settings s = Settings::parse(ini, &warnings);
    CHECK(warnings.empty());
    CHECK(!s.enabledAtStart);
    CHECK(s.reticleColor == 0xFF8800);
    CHECK(!s.freeDepthFromFixation);
    CHECK_NEAR(s.freeDepthMeters, 1.5, 1e-6);
    CHECK_NEAR(s.filter.beta, 0.1, 1e-6);
    CHECK(s.pointer.deadbandPx == 10);
    CHECK(s.targets.size() == 1);
    CHECK(s.targets[0].desktop.kind == DesktopSelector::Kind::MonitorIndex);
    CHECK(s.targets[0].desktop.monitorIndex == 1);
}

TEST("settings: bad values warn and keep defaults") {
    const char* ini = R"(
[pointer]
reticle_degrees = -3
reticle_color = blue
nonsense = 1
[filter]
min_cutoff_hz = abc
[bogus]
x = 1
[target.nokey]
mode = desktop
[target.badmode]
key = a.b
mode = laser
)";
    std::vector<std::string> warnings;
    Settings s = Settings::parse(ini, &warnings);
    Settings d;
    CHECK(warnings.size() == 7);
    CHECK_NEAR(s.reticleDegrees, d.reticleDegrees, 1e-6);
    CHECK(s.reticleColor == d.reticleColor);
    CHECK_NEAR(s.filter.minCutoffHz, d.filter.minCutoffHz, 1e-6);
    CHECK(s.targets.size() == 1);
    CHECK(s.targets[0].key == "a.b");
    CHECK(s.targets[0].mode == TargetConfig::Mode::Pointer);
}

TEST("settings: no target sections means default targets") {
    Settings s = Settings::parse("[pointer]\nenabled = true\n");
    CHECK(s.targets.size() == 2);
}
