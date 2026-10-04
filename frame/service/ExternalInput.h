#pragma once

#include "InputSource.h"

#include <map>
#include <memory>
#include <string>

namespace frame {

/// Receives external input messages (eyepointer::parseInputMessage) on the abstract
/// unix datagram socket "@frameeyepointer_input", from any process of the same
/// user: a hand tracker, a test script. Each sender names itself ("hands"), and
/// [inputs] aim_sources / button_sources list the names to use.
///
/// Senders should repeat their state (aim and held buttons) every frame, or at
/// least within [inputs] external_timeout_ms: a source that goes quiet for longer
/// stops aiming and lets go of its buttons.
class ExternalInputHub {
public:
    static constexpr const char* kSocketName = "frameeyepointer_input";

    ExternalInputHub();
    ~ExternalInputHub();

    /// Reads all waiting messages. Call once per frame.
    void poll(double time);

    eyepointer::Aim aim(const std::string& source, double time, double timeout) const;
    eyepointer::ButtonState buttons(const std::string& source, double time, double timeout) const;
    /// Seconds since `source` last sent anything, or a negative number if never.
    double silentFor(const std::string& source, double time) const;
    bool listening() const { return fd_ >= 0; }

private:
    struct State {
        double lastSeen = -1e9;
        double aimAt = -1e9;
        eyepointer::Aim aim;
        eyepointer::ButtonState buttons;
    };
    int fd_ = -1;
    std::map<std::string, State> sources_;
    int rejected_ = 0;
};

/// One named external source, as an InputSource.
class ExternalSource : public InputSource {
public:
    ExternalSource(std::string name, ExternalInputHub& hub) : name_(std::move(name)), hub_(hub) {}
    std::string name() const override { return name_; }
    bool providesAim() const override { return true; }
    eyepointer::Aim readAim(const FrameContext& ctx) override;
    bool providesButtons() const override { return true; }
    eyepointer::ButtonState readButtons(const FrameContext& ctx) override;
    std::string status() const override;
    void probe(const FrameContext& ctx) override;

private:
    std::string name_;
    ExternalInputHub& hub_;
    double lastTime_ = 0;
};

} // namespace frame
