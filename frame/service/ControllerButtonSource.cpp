#include "ControllerButtonSource.h"

#include <cmath>

namespace frame {

ControllerButtonSource::ControllerButtonSource() {
    auto* in = vr::VRInput();
    bool ok = true;
    ok &= in->GetActionSetHandle("/actions/click", &setClick_) == vr::VRInputError_None;
    ok &= in->GetActionSetHandle("/actions/rightclick", &setRight_) == vr::VRInputError_None;
    ok &= in->GetActionSetHandle("/actions/toggle", &setToggle_) == vr::VRInputError_None;
    ok &= in->GetActionSetHandle("/actions/scroll", &setScroll_) == vr::VRInputError_None;
    ok &= in->GetActionHandle("/actions/click/in/press", &click_) == vr::VRInputError_None;
    ok &= in->GetActionHandle("/actions/rightclick/in/press", &right_) == vr::VRInputError_None;
    ok &= in->GetActionHandle("/actions/toggle/in/press", &toggle_) == vr::VRInputError_None;
    ok &= in->GetActionHandle("/actions/scroll/in/stick", &scroll_) == vr::VRInputError_None;
    ok_ = ok;
    if (!ok) logf("controllers: button actions are missing from the action manifest");
}

void ControllerButtonSource::collectActionSets(const FrameContext& ctx,
                                               std::vector<vr::VRActiveActionSet_t>& sets) {
    if (!ok_) return;
    if (takeToggle(ctx)) addActionSet(sets, setToggle_);
    if (takePointer(ctx)) {
        addActionSet(sets, setClick_);
        addActionSet(sets, setRight_);
        addActionSet(sets, setScroll_);
    }
}

bool ControllerButtonSource::digital(vr::VRActionHandle_t action) const {
    vr::InputDigitalActionData_t d{};
    return vr::VRInput()->GetDigitalActionData(action, &d, sizeof d, vr::k_ulInvalidInputValueHandle) ==
               vr::VRInputError_None &&
           d.bActive && d.bState;
}

eyepointer::ButtonState ControllerButtonSource::readButtons(const FrameContext& ctx) {
    eyepointer::ButtonState b;
    if (!ok_) return b;
    if (takeToggle(ctx)) b.toggle = digital(toggle_);
    if (takePointer(ctx)) {
        b.click = digital(click_);
        b.rightClick = digital(right_);
        vr::InputAnalogActionData_t a{};
        if (vr::VRInput()->GetAnalogActionData(scroll_, &a, sizeof a, vr::k_ulInvalidInputValueHandle) ==
                vr::VRInputError_None &&
            a.bActive && std::fabs(a.y) > 0.25f)
            b.scroll = a.y;
    }
    return b;
}

void ControllerButtonSource::probe(const FrameContext&) {
    bool global = vr::VRSettings()->GetBool("steamvr", "globalActionSetPriority", nullptr);
    logf("controllers: actions %s, SteamVR global input from overlays %s", ok_ ? "ok" : "MISSING",
         global ? "on" : "off");
}

} // namespace frame
