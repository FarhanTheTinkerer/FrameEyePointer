#include "eyepointer/ClickSequencer.h"

#include <algorithm>

namespace eyepointer {

void ClickSequencer::emit(LaserButtonEvent e, std::vector<LaserButtonEvent>& out) {
    bool& state = down_[static_cast<int>(e.button)];
    if (state == e.down) return; // never send a duplicate edge
    state = e.down;
    out.push_back(e);
}

void ClickSequencer::scheduleClaim(double at) {
    // One claim pulse at a time.
    for (const auto& p : pending_)
        if (p.event.button == LaserButton::Claim) return;
    schedule(at, LaserButton::Claim, true);
    schedule(at + timing_.claimHoldSeconds, LaserButton::Claim, false);
    lastClaim_ = at;
}

void ClickSequencer::handle(LaserButton button, bool held, bool& prev, const Input& in) {
    bool pressed = held && !prev;
    bool released = !held && prev;
    prev = held;
    if (!in.active) return;
    if (pressed) {
        schedule(in.time, button, true);
        if (timing_.mode == ClickTiming::Mode::Tap) {
            schedule(in.time + timing_.tapSeconds, button, false);
            scheduleClaim(in.time + timing_.reclaimDelaySeconds);
        }
    } else if (released) {
        if (timing_.mode == ClickTiming::Mode::Hold) schedule(in.time, button, false);
        scheduleClaim(in.time + timing_.reclaimDelaySeconds);
    }
}

std::vector<LaserButtonEvent> ClickSequencer::update(const Input& in) {
    std::vector<LaserButtonEvent> out;

    if (!in.active) {
        // Inactive: drop everything planned and let go of anything held.
        pending_.clear();
        prevClick_ = in.clickHeld;
        prevRight_ = in.rightClickHeld;
        for (LaserButton b : {LaserButton::Click, LaserButton::RightClick, LaserButton::Claim})
            emit({b, false}, out);
        return out;
    }

    handle(LaserButton::Click, in.clickHeld, prevClick_, in);
    handle(LaserButton::RightClick, in.rightClickHeld, prevRight_, in);

    bool holding = down_[0] || down_[1] || in.clickHeld || in.rightClickHeld;
    if (in.wantClaim && !holding && in.time - lastClaim_ >= timing_.claimIntervalSeconds)
        scheduleClaim(in.time);

    // Emit everything due, in time order (stable for equal times).
    std::stable_sort(pending_.begin(), pending_.end(),
                     [](const Scheduled& a, const Scheduled& b) { return a.at < b.at; });
    size_t i = 0;
    for (; i < pending_.size() && pending_[i].at <= in.time; ++i) emit(pending_[i].event, out);
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(i));
    return out;
}

} // namespace eyepointer
