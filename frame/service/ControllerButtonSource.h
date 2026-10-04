#pragma once

#include "InputSource.h"

namespace frame {

/// "controllers": the Frame controller buttons. Defaults (rebindable in SteamVR's
/// binding UI under FrameEyePointer): right bumper = click, left bumper = right
/// click, left View = toggle (held), right thumbstick = scroll.
///
/// Each button is its own action set at an overlay-global priority, so SteamVR
/// delivers it while other apps have input focus and only the buttons in use are
/// taken from them. Taken only outside games, or with the dashboard open.
class ControllerButtonSource : public InputSource {
public:
    /// After SetActionManifestPath.
    ControllerButtonSource();

    std::string name() const override { return "controllers"; }
    void collectActionSets(const FrameContext& ctx, std::vector<vr::VRActiveActionSet_t>& sets) override;
    bool providesButtons() const override { return true; }
    eyepointer::ButtonState readButtons(const FrameContext& ctx) override;
    void probe(const FrameContext& ctx) override;

private:
    bool takeToggle(const FrameContext& ctx) const { return !ctx.inGame || ctx.dashboardVisible; }
    bool takePointer(const FrameContext& ctx) const { return ctx.enabled && ctx.allowed; }
    bool digital(vr::VRActionHandle_t action) const;

    bool ok_ = false;
    vr::VRActionSetHandle_t setClick_ = 0, setRight_ = 0, setToggle_ = 0, setScroll_ = 0;
    vr::VRActionHandle_t click_ = 0, right_ = 0, toggle_ = 0, scroll_ = 0;
};

} // namespace frame
