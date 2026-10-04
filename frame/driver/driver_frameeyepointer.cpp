// frameeyepointer: a virtual SteamVR controller whose ray is your gaze (or another
// aim source, such as a hand ray).
//
// SteamVR's own laser mouse follows this device, so wherever you look it points
// at the dashboard, Steam, overlays and desktop panels like a controller laser
// would, and its buttons click there. The service (frameeyepointerd) tells it
// where the eyes look, relative to the head; the driver combines that with the
// freshest headset pose every frame, so turning your head never lags. The ray
// starts at the eyes, so the beam is seen end-on and only the hit dot shows.
//
// Adapted from Frametop's ft_pointer driver (pointer/driver/driver_ft_pointer.cpp,
// https://github.com/DeeJanuz/frametop, MIT License, Copyright (c) 2026 DeeJanuz).
//
// Control socket: abstract unix datagram "@frameeyepointer", text commands:
//   gaze <ox> <oy> <oz> <dx> <dy> <dz>   ray origin and unit direction in head space
//                                        (metres, -Z forward): eyes
//   ray <ox> <oy> <oz> <dx> <dy> <dz>    ray in SteamVR's raw tracking space: a hand
//   btn <trigger|b|a> <0|1>              trigger = click, b = right click,
//                                        a = take the laser (switchlaserhand), no click
//   scroll <x> <y>                       joystick -1..1
//   show | hide                          connect / disconnect the device
//
// A press that is released before the next frame is still reported for one
// frame, so quick taps are never lost.
//
// Role: steamvr.vrsettings "driver_frameeyepointer" / "role" (int). Default 4
// (treadmill), so the device never takes a hand role from the real controllers.
// SteamVR only gives the /user/treadmill path to a device that hints that role
// when it activates, so the role is fixed for the session.
#include <openvr_driver.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace vr;

namespace {

constexpr const char* kSocketName = "frameeyepointer";
constexpr int kButtons = 3;
const char* const kButtonNames[kButtons] = {"trigger", "b", "a"};
const char* const kButtonPaths[kButtons] = {"/input/trigger/click", "/input/b/click", "/input/a/click"};

struct State {
    std::mutex lock;
    bool visible = false;
    bool haveGaze = false;
    bool headRelative = true; // "gaze" (true) or "ray" (false)
    double origin[3] = {0, 0, 0};
    double dir[3] = {0, 0, -1};
    bool buttons[kButtons] = {};
    bool latched[kButtons] = {}; // pressed since the last frame
    float scrollX = 0, scrollY = 0;
};

// Double-precision math throughout: keeps the driver to the oldest libm symbols.
HmdQuaternion_t QuatFromMatrix(const double m[3][3]) {
    const double trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0) {
        const double s = 0.5 / std::sqrt(trace + 1.0);
        return {0.25 / s, (m[2][1] - m[1][2]) * s, (m[0][2] - m[2][0]) * s, (m[1][0] - m[0][1]) * s};
    }
    if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const double s = 2.0 * std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]);
        return {(m[2][1] - m[1][2]) / s, 0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s};
    }
    if (m[1][1] > m[2][2]) {
        const double s = 2.0 * std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]);
        return {(m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s};
    }
    const double s = 2.0 * std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]);
    return {(m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s};
}

void Normalize(double v[3]) {
    const double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < 1e-9) {
        v[0] = 0, v[1] = 0, v[2] = -1;
        return;
    }
    for (int i = 0; i < 3; ++i) v[i] /= len;
}

void Cross(const double a[3], const double b[3], double out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

class EyeLaserDevice : public ITrackedDeviceServerDriver {
public:
    explicit EyeLaserDevice(State* state) : state_(state) {}

    EVRInitError Activate(uint32_t objectId) override {
        objectId_ = objectId;
        auto* props = VRProperties();
        container_ = props->TrackedDeviceToPropertyContainer(objectId);
        EVRSettingsError err = VRSettingsError_None;
        int32_t role = VRSettings()->GetInt32("driver_frameeyepointer", "role", &err);
        if (err != VRSettingsError_None || role <= 0) role = TrackedControllerRole_Treadmill;

        props->SetStringProperty(container_, Prop_ModelNumber_String, "frameeyepointer");
        props->SetStringProperty(container_, Prop_ManufacturerName_String, "FrameEyePointer");
        props->SetStringProperty(container_, Prop_SerialNumber_String, "frameeyepointer_0");
        props->SetStringProperty(container_, Prop_ControllerType_String, "frameeyepointer");
        props->SetStringProperty(container_, Prop_InputProfilePath_String,
                                 "{frameeyepointer}/input/frameeyepointer_profile.json");
        props->SetStringProperty(container_, Prop_RenderModelName_String,
                                 "{frameeyepointer}/rendermodels/frameeyepointer_invisible");
        props->SetInt32Property(container_, Prop_ControllerRoleHint_Int32, role);
        props->SetInt32Property(container_, Prop_DeviceClass_Int32, TrackedDeviceClass_Controller);
        props->SetBoolProperty(container_, Prop_NeverTracked_Bool, false);

        auto* input = VRDriverInput();
        for (int i = 0; i < kButtons; ++i) input->CreateBooleanComponent(container_, kButtonPaths[i], &buttons_[i]);
        input->CreateScalarComponent(container_, "/input/joystick/x", &scrollX_, VRScalarType_Absolute,
                                     VRScalarUnits_NormalizedTwoSided);
        input->CreateScalarComponent(container_, "/input/joystick/y", &scrollY_, VRScalarType_Absolute,
                                     VRScalarUnits_NormalizedTwoSided);

        char msg[96];
        std::snprintf(msg, sizeof msg, "frameeyepointer: activated, role %d", int(role));
        VRDriverLog()->Log(msg);
        return VRInitError_None;
    }

    void Deactivate() override { objectId_ = k_unTrackedDeviceIndexInvalid; }
    void EnterStandby() override {}
    void* GetComponent(const char*) override { return nullptr; }
    void DebugRequest(const char*, char* response, uint32_t size) override {
        if (size) response[0] = 0;
    }
    DriverPose_t GetPose() override { return pose_; }

    void RunFrame() {
        if (objectId_ == k_unTrackedDeviceIndexInvalid) return;
        TrackedDevicePose_t hmd{};
        VRServerDriverHost()->GetRawTrackedDevicePoses(0.f, &hmd, 1);

        bool visible, haveGaze, headRelative;
        double o[3], d[3];
        bool pressed[kButtons];
        float sx, sy;
        {
            std::lock_guard<std::mutex> guard(state_->lock);
            visible = state_->visible;
            haveGaze = state_->haveGaze;
            headRelative = state_->headRelative;
            std::memcpy(o, state_->origin, sizeof o);
            std::memcpy(d, state_->dir, sizeof d);
            for (int i = 0; i < kButtons; ++i) {
                pressed[i] = state_->buttons[i] || state_->latched[i];
                state_->latched[i] = false;
            }
            sx = state_->scrollX;
            sy = state_->scrollY;
        }

        DriverPose_t pose{};
        pose.qWorldFromDriverRotation.w = 1;
        pose.qDriverFromHeadRotation.w = 1;
        const auto& m = hmd.mDeviceToAbsoluteTracking.m;

        // Ray frame: -Z along the ray, X level. In head space for "gaze" (then composed
        // with the headset pose), in tracking space for "ray".
        Normalize(d);
        double z[3] = {-d[0], -d[1], -d[2]};
        const double up[3] = {0, 1, 0};
        double x[3];
        Cross(up, z, x);
        if (x[0] * x[0] + x[1] * x[1] + x[2] * x[2] < 1e-8) x[0] = 1, x[1] = 0, x[2] = 0;
        Normalize(x);
        double y[3];
        Cross(z, x, y);

        // World rotation = head rotation * ray frame; position = head * origin.
        double r[3][3];
        if (!headRelative) {
            for (int row = 0; row < 3; ++row) {
                r[row][0] = x[row];
                r[row][1] = y[row];
                r[row][2] = z[row];
                pose.vecPosition[row] = o[row];
            }
        }
        for (int row = 0; row < 3 && headRelative; ++row) {
            r[row][0] = m[row][0] * x[0] + m[row][1] * x[1] + m[row][2] * x[2];
            r[row][1] = m[row][0] * y[0] + m[row][1] * y[1] + m[row][2] * y[2];
            r[row][2] = m[row][0] * z[0] + m[row][1] * z[1] + m[row][2] * z[2];
            pose.vecPosition[row] = m[row][0] * o[0] + m[row][1] * o[1] + m[row][2] * o[2] + m[row][3];
        }
        pose.qRotation = QuatFromMatrix(r);

        const bool ok = (hmd.bPoseIsValid || !headRelative) && visible && haveGaze;
        pose.poseIsValid = ok;
        pose.result = ok ? TrackingResult_Running_OK : TrackingResult_Uninitialized;
        pose.deviceIsConnected = visible;
        pose_ = pose;
        VRServerDriverHost()->TrackedDevicePoseUpdated(objectId_, pose_, sizeof(DriverPose_t));

        auto* input = VRDriverInput();
        for (int i = 0; i < kButtons; ++i) input->UpdateBooleanComponent(buttons_[i], visible && pressed[i], 0);
        input->UpdateScalarComponent(scrollX_, visible ? sx : 0.f, 0);
        input->UpdateScalarComponent(scrollY_, visible ? sy : 0.f, 0);
    }

private:
    State* state_;
    uint32_t objectId_ = k_unTrackedDeviceIndexInvalid;
    PropertyContainerHandle_t container_ = k_ulInvalidPropertyContainer;
    DriverPose_t pose_{};
    VRInputComponentHandle_t buttons_[kButtons] = {};
    VRInputComponentHandle_t scrollX_ = 0, scrollY_ = 0;
};

class Provider : public IServerTrackedDeviceProvider {
public:
    EVRInitError Init(IVRDriverContext* context) override {
        VR_INIT_SERVER_DRIVER_CONTEXT(context);
        EVRSettingsError err = VRSettingsError_None;
        bool enable = VRSettings()->GetBool("driver_frameeyepointer", "enable", &err);
        if (err == VRSettingsError_None && !enable) return VRInitError_None;
        device_ = new EyeLaserDevice(&state_);
        VRServerDriverHost()->TrackedDeviceAdded("frameeyepointer_0", TrackedDeviceClass_Controller, device_);
        running_ = true;
        listener_ = std::thread([this] { Listen(); });
        return VRInitError_None;
    }

    void Cleanup() override {
        running_ = false;
        if (sock_ >= 0) shutdown(sock_, SHUT_RDWR);
        if (listener_.joinable()) listener_.join();
        if (sock_ >= 0) close(sock_);
        sock_ = -1;
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    }

    const char* const* GetInterfaceVersions() override { return k_InterfaceVersions; }
    void RunFrame() override {
        if (device_) device_->RunFrame();
    }
    bool ShouldBlockStandbyMode() override { return false; }
    void EnterStandby() override {}
    void LeaveStandby() override {}

private:
    void Listen() {
        sock_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const size_t n = std::strlen(kSocketName);
        std::memcpy(addr.sun_path + 1, kSocketName, n); // abstract namespace
        const socklen_t len = socklen_t(offsetof(sockaddr_un, sun_path) + 1 + n);
        if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
            VRDriverLog()->Log("frameeyepointer: cannot bind the control socket");
            return;
        }
        timeval tv{0, 200000};
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        char buf[256];
        while (running_) {
            const ssize_t got = recv(sock_, buf, sizeof buf - 1, 0);
            if (got <= 0) continue;
            buf[got] = 0;
            Handle(buf);
        }
    }

    void Handle(const char* cmd) {
        std::lock_guard<std::mutex> guard(state_.lock);
        double v[6];
        char name[16];
        int pressed;
        float a, b;
        bool gaze = std::sscanf(cmd, "gaze %lf %lf %lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6;
        bool ray = !gaze && std::sscanf(cmd, "ray %lf %lf %lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6;
        if (gaze || ray) {
            bool finite = true;
            for (double x : v) finite = finite && std::isfinite(x);
            if (!finite) return;
            std::memcpy(state_.origin, v, sizeof state_.origin);
            std::memcpy(state_.dir, v + 3, sizeof state_.dir);
            state_.haveGaze = true;
            state_.headRelative = gaze;
        } else if (std::sscanf(cmd, "btn %15s %d", name, &pressed) == 2) {
            for (int i = 0; i < kButtons; ++i) {
                if (std::strcmp(name, kButtonNames[i]) != 0) continue;
                if (pressed && !state_.buttons[i]) state_.latched[i] = true;
                state_.buttons[i] = pressed != 0;
            }
        } else if (std::sscanf(cmd, "scroll %f %f", &a, &b) == 2) {
            state_.scrollX = std::fmax(-1.f, std::fmin(1.f, a));
            state_.scrollY = std::fmax(-1.f, std::fmin(1.f, b));
        } else if (std::strncmp(cmd, "show", 4) == 0) {
            state_.visible = true;
        } else if (std::strncmp(cmd, "hide", 4) == 0) {
            state_.visible = false;
            for (int i = 0; i < kButtons; ++i) state_.buttons[i] = state_.latched[i] = false;
            state_.scrollX = state_.scrollY = 0;
        }
    }

    State state_;
    EyeLaserDevice* device_ = nullptr;
    std::thread listener_;
    std::atomic<bool> running_{false};
    int sock_ = -1;
};

Provider g_provider;

} // namespace

extern "C" __attribute__((visibility("default"))) void* HmdDriverFactory(const char* interfaceName,
                                                                         int* returnCode) {
    if (std::strcmp(interfaceName, IServerTrackedDeviceProvider_Version) == 0) return &g_provider;
    if (returnCode) *returnCode = VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
