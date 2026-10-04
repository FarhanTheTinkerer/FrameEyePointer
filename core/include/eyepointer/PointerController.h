#pragma once

#include "eyepointer/DesktopMapping.h"

#include <optional>
#include <vector>

namespace eyepointer {

enum class MouseButton { Left = 0, Right = 1 };

struct MouseCommand {
    enum class Type { Move, ButtonDown, ButtonUp, Wheel };
    Type type = Type::Move;
    Point position;                        ///< Move
    MouseButton button = MouseButton::Left; ///< ButtonDown / ButtonUp
    int wheelDelta = 0;                    ///< Wheel, in WHEEL_DELTA units (120 per notch)
};

struct PointerParams {
    /// The OS cursor only moves when the gaze point moves at least this far.
    /// Keeps the cursor still while you fixate.
    int deadbandPx = 6;
    /// While a button is held the cursor stays where it was pressed until the gaze
    /// moves this far, then it drags.
    int dragThresholdPx = 40;
    /// Thumbstick values below this are ignored for scrolling.
    float scrollDeadzone = 0.3f;
    /// Wheel notches per second at full thumbstick deflection.
    float scrollNotchesPerSecond = 12.0f;
};

struct PointerInput {
    double timeSeconds = 0;
    bool enabled = true;
    /// Gaze point on a desktop-mapped overlay, if the gaze is on one.
    std::optional<Point> gazePixel;
    bool leftHeld = false;
    bool rightHeld = false;
    float scroll = 0; ///< -1..1, positive scrolls up
};

/// Turns gaze points and button states into OS mouse commands: deadbanded
/// cursor movement, clicks that land where you were looking when you pressed,
/// eye-driven drags past a threshold, and thumbstick scrolling.
class PointerController {
public:
    explicit PointerController(PointerParams params = {}) : params_(params) {}

    void setParams(const PointerParams& params) { params_ = params; }

    std::vector<MouseCommand> update(const PointerInput& input);

    /// Releases any held buttons (for example on shutdown).
    std::vector<MouseCommand> releaseAll();

    bool buttonHeld(MouseButton b) const { return held_[static_cast<int>(b)].active; }

private:
    struct Held {
        bool active = false;
        bool dragging = false;
        Point pressPos;
    };

    void moveTo(const Point& p, std::vector<MouseCommand>& out);
    void handleButton(MouseButton button, bool down, const PointerInput& input,
                      std::vector<MouseCommand>& out);

    PointerParams params_;
    Held held_[2];
    bool prevDown_[2] = {false, false};
    bool hasCursor_ = false;
    Point cursor_;
    double lastTime_ = -1;
    float scrollAccum_ = 0;
};

} // namespace eyepointer
