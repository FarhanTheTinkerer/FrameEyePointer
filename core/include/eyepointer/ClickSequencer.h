#pragma once

#include <vector>

namespace eyepointer {

/// Buttons of the virtual eye-laser controller (see frame/driver).
enum class LaserButton {
    Click,      ///< "trigger": laser mouse left click
    RightClick, ///< "b": laser mouse right click
    Claim,      ///< "a": switchlaserhand, makes the eye laser the active laser (no click)
};

struct LaserButtonEvent {
    LaserButton button;
    bool down;
    bool operator==(const LaserButtonEvent& o) const { return button == o.button && down == o.down; }
};

struct ClickTiming {
    enum class Mode {
        /// A press sends a complete click right away (down, then up `tapMs` later).
        /// Steam drops SteamVR out of laser mode ~40 ms after any Frame controller
        /// press, so a click must finish before that. No drags.
        Tap,
        /// Button down while the controller button is held: allows drags, but the
        /// laser-mode switch can cut them short.
        Hold,
    };
    Mode mode = Mode::Tap;
    double tapSeconds = 0.025;
    /// After a controller press or release, take the laser back this much later
    /// (after Steam's switch) ...
    double reclaimDelaySeconds = 0.08;
    /// ... holding the claim button this long.
    double claimHoldSeconds = 0.06;
    /// Minimum time between claims while something else holds the laser.
    double claimIntervalSeconds = 0.5;
};

/// Turns controller button states into the virtual controller's button events.
/// Pure timing logic, driven by update() every frame.
class ClickSequencer {
public:
    explicit ClickSequencer(ClickTiming timing = {}) : timing_(timing) {}
    void setTiming(const ClickTiming& t) { timing_ = t; }

    struct Input {
        double time = 0;
        /// The eye laser is connected and may click.
        bool active = false;
        bool clickHeld = false;      ///< controller button for left click
        bool rightClickHeld = false; ///< controller button for right click
        /// Another device holds SteamVR's laser and we want it back.
        bool wantClaim = false;
    };

    std::vector<LaserButtonEvent> update(const Input& in);

private:
    struct Scheduled {
        double at;
        LaserButtonEvent event;
    };

    void schedule(double at, LaserButton b, bool down) { pending_.push_back({at, {b, down}}); }
    void scheduleClaim(double at);
    void handle(LaserButton button, bool held, bool& prev, const Input& in);
    void emit(LaserButtonEvent e, std::vector<LaserButtonEvent>& out);

    ClickTiming timing_;
    std::vector<Scheduled> pending_;
    bool prevClick_ = false, prevRight_ = false;
    bool down_[3] = {false, false, false};
    double lastClaim_ = -1e9;
};

} // namespace eyepointer
