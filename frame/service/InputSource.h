#pragma once

// The service's input modules. An input source can aim the laser, press its
// buttons, or both. Built in: "eyes" (EyeGazeSource) and "controllers"
// (ControllerButtonSource). Any other name in [inputs] is an external source fed
// over a socket (ExternalInput.h), which is how a hand tracker plugs in without
// changing this program. To add a built-in source, implement InputSource and
// register it in makeSource() in frameeyepointerd.cpp.

#include "eyepointer/FrameSettings.h"
#include "eyepointer/InputSources.h"
#include "eyepointer/Math.h"

#include <openvr.h>

#include <string>
#include <vector>

namespace frame {

/// What every source may look at this frame.
struct FrameContext {
    double time = 0;
    const eyepointer::FrameSettings* settings = nullptr;
    bool enabled = true;          ///< the eye laser is switched on
    bool inGame = false;          ///< a scene application (game) runs
    bool dashboardVisible = false;
    bool worn = true;             ///< the headset is on someone's head
    /// The laser may be used now: worn, and no game or the dashboard is open over it.
    bool allowed = true;
    bool headValid = false;
    eyepointer::Mat34 headStanding; ///< headset pose, standing space
};

class InputSource {
public:
    virtual ~InputSource() = default;
    virtual std::string name() const = 0;

    /// SteamVR action sets to activate this frame. The service activates every
    /// source's sets in one UpdateActionState call before reading any of them.
    virtual void collectActionSets(const FrameContext&, std::vector<vr::VRActiveActionSet_t>&) {}

    virtual bool providesAim() const { return false; }
    virtual eyepointer::Aim readAim(const FrameContext&) { return {}; }

    virtual bool providesButtons() const { return false; }
    virtual eyepointer::ButtonState readButtons(const FrameContext&) { return {}; }

    /// One line of state for the periodic status log.
    virtual std::string status() const { return {}; }
    /// Diagnostics for --probe, printed to stdout.
    virtual void probe(const FrameContext&) {}
};

/// Activates action sets at an overlay-global priority, so they are delivered while
/// another app or the dashboard has input focus (needs SteamVR's "Enable global
/// input from overlays").
constexpr int32_t kGlobalActionSetPriority = vr::k_nActionSetOverlayGlobalPriorityMin + 0x100;

inline void addActionSet(std::vector<vr::VRActiveActionSet_t>& sets, vr::VRActionSetHandle_t handle) {
    vr::VRActiveActionSet_t s{};
    s.ulActionSet = handle;
    s.nPriority = kGlobalActionSetPriority;
    sets.push_back(s);
}

void logf(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

} // namespace frame
