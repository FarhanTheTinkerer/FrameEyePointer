#include "eyepointer/PointerController.h"

#include <cmath>

namespace eyepointer {

namespace {

constexpr int kWheelDelta = 120;

int distanceSq(const Point& a, const Point& b) {
    int dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

} // namespace

void PointerController::moveTo(const Point& p, std::vector<MouseCommand>& out) {
    if (hasCursor_ && cursor_ == p) return;
    MouseCommand c;
    c.type = MouseCommand::Type::Move;
    c.position = p;
    out.push_back(c);
    cursor_ = p;
    hasCursor_ = true;
}

void PointerController::handleButton(MouseButton button, bool down, const PointerInput& input,
                                     std::vector<MouseCommand>& out) {
    int i = static_cast<int>(button);
    bool wasDown = prevDown_[i];
    prevDown_[i] = down;
    Held& h = held_[i];

    if (down && !wasDown) {
        // A press only counts if you are looking at a desktop.
        if (!input.gazePixel) return;
        Point target = *input.gazePixel;
        // Prefer the steady cursor position if the gaze is still within the deadband,
        // so fixation jitter at the moment of the press does not shift the click.
        int deadSq = params_.deadbandPx * params_.deadbandPx;
        if (hasCursor_ && distanceSq(cursor_, target) < deadSq) target = cursor_;
        moveTo(target, out);
        h = {true, false, target};
        MouseCommand c;
        c.type = MouseCommand::Type::ButtonDown;
        c.button = button;
        out.push_back(c);
    } else if (!down && wasDown && h.active) {
        h = {};
        MouseCommand c;
        c.type = MouseCommand::Type::ButtonUp;
        c.button = button;
        out.push_back(c);
    }
}

std::vector<MouseCommand> PointerController::update(const PointerInput& input) {
    std::vector<MouseCommand> out;
    double dt = lastTime_ < 0 ? 0.0 : input.timeSeconds - lastTime_;
    if (dt < 0 || dt > 0.25) dt = 0;
    lastTime_ = input.timeSeconds;

    if (!input.enabled) {
        out = releaseAll();
        prevDown_[0] = input.leftHeld;
        prevDown_[1] = input.rightHeld;
        scrollAccum_ = 0;
        return out;
    }

    handleButton(MouseButton::Left, input.leftHeld, input, out);
    handleButton(MouseButton::Right, input.rightHeld, input, out);

    if (input.gazePixel) {
        const Point& gaze = *input.gazePixel;
        bool anyHeld = held_[0].active || held_[1].active;
        if (anyHeld) {
            for (Held& h : held_) {
                if (!h.active || h.dragging) continue;
                int t = params_.dragThresholdPx;
                if (distanceSq(gaze, h.pressPos) >= t * t) h.dragging = true;
            }
            bool dragging = (held_[0].active && held_[0].dragging) ||
                            (held_[1].active && held_[1].dragging);
            int deadSq = params_.deadbandPx * params_.deadbandPx;
            if (dragging && distanceSq(gaze, cursor_) >= deadSq) moveTo(gaze, out);
        } else {
            int deadSq = params_.deadbandPx * params_.deadbandPx;
            if (!hasCursor_ || distanceSq(gaze, cursor_) >= deadSq) moveTo(gaze, out);
        }

        float s = input.scroll;
        if (std::fabs(s) > params_.scrollDeadzone) {
            float sign = s > 0 ? 1.0f : -1.0f;
            float mag = (std::fabs(s) - params_.scrollDeadzone) / (1.0f - params_.scrollDeadzone);
            scrollAccum_ += sign * mag * params_.scrollNotchesPerSecond * static_cast<float>(dt);
            int notches = static_cast<int>(scrollAccum_);
            if (notches != 0) {
                scrollAccum_ -= static_cast<float>(notches);
                MouseCommand c;
                c.type = MouseCommand::Type::Wheel;
                c.wheelDelta = notches * kWheelDelta;
                out.push_back(c);
            }
        } else {
            scrollAccum_ = 0;
        }
    } else {
        // Looking away from every desktop: leave the OS cursor alone so a real
        // mouse keeps working, and forget the last position.
        if (!held_[0].active && !held_[1].active) hasCursor_ = false;
        scrollAccum_ = 0;
    }
    return out;
}

std::vector<MouseCommand> PointerController::releaseAll() {
    std::vector<MouseCommand> out;
    for (int i = 0; i < 2; ++i) {
        if (!held_[i].active) continue;
        held_[i] = {};
        MouseCommand c;
        c.type = MouseCommand::Type::ButtonUp;
        c.button = static_cast<MouseButton>(i);
        out.push_back(c);
    }
    return out;
}

} // namespace eyepointer
