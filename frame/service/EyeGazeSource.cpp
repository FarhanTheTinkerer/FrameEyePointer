#include "EyeGazeSource.h"

#include "eyepointer/openvr/OpenVrGaze.h"

#include <cmath>
#include <cstdio>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace frame {

using eyepointer::Aim;
using eyepointer::EyeServerStatus;
using Src = eyepointer::FrameSettings::GazeSource;

namespace {

double nowRaw() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

double yawDeg(const eyepointer::Vec3& d) { return std::atan2(-d.x, -d.z) * 180.0 / M_PI; }
double pitchDeg(const eyepointer::Vec3& d) {
    return std::asin(std::fmax(-1.0, std::fmin(1.0, double(d.y)))) * 180.0 / M_PI;
}

} // namespace

// ---------------------------------------------------------------------------

bool EyeServerFile::open() {
    close();
    int fd = ::open(kPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        return false;
    }
    void* m = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (m == MAP_FAILED) return false;
    data_ = static_cast<const volatile uint8_t*>(m);
    size_ = size_t(st.st_size);
    return true;
}

void EyeServerFile::close() {
    if (data_) munmap(const_cast<uint8_t*>(data_), size_);
    data_ = nullptr;
    size_ = 0;
}

EyeServerStatus EyeServerFile::read(eyepointer::EyeServerSample& s) const {
    if (!data_) return EyeServerStatus::TooSmall;
    EyeServerStatus st = EyeServerStatus::Torn;
    for (int attempt = 0; attempt < 4 && st == EyeServerStatus::Torn; ++attempt)
        st = eyepointer::parseEyeServer(data_, size_, s);
    return st;
}

// ---------------------------------------------------------------------------

EyeGazeSource::EyeGazeSource() {
    auto* in = vr::VRInput();
    if (in->GetActionSetHandle("/actions/gaze", &set_) != vr::VRInputError_None ||
        in->GetActionHandle("/actions/gaze/in/gaze", &action_) != vr::VRInputError_None) {
        set_ = vr::k_ulInvalidActionSetHandle;
        logf("eyes: the gaze action is missing from the action manifest");
    }
}

bool EyeGazeSource::useAction(const FrameContext& ctx) const {
    // Never during games: see the class comment.
    return set_ != vr::k_ulInvalidActionSetHandle && ctx.settings->gazeSource != Src::Mmap && !ctx.inGame;
}

bool EyeGazeSource::useMmap(const FrameContext& ctx) const {
    return ctx.settings->gazeSource == Src::Mmap || (ctx.settings->gazeSource == Src::Auto && ctx.inGame);
}

void EyeGazeSource::collectActionSets(const FrameContext& ctx, std::vector<vr::VRActiveActionSet_t>& sets) {
    if (ctx.enabled && ctx.allowed && useAction(ctx)) addActionSet(sets, set_);
}

Aim EyeGazeSource::readAction(const FrameContext& ctx) {
    Aim a;
    vr::EVRInputError e = vr::VRInputError_None;
    eyepointer::RawGaze raw = eyepointer::openvr::readGaze(action_, vr::TrackingUniverseStanding, 0.0f, &e);
    if (e != vr::VRInputError_None) {
        if (!warnedAction_) logf("eyes: reading SteamVR eye tracking failed (error %d)", int(e));
        warnedAction_ = true;
        return a;
    }
    warnedAction_ = false;
    if (!raw.valid || !ctx.headValid) return a;
    eyepointer::Vec3 d = eyepointer::normalize(raw.target - raw.origin);
    if (eyepointer::length(d) < 0.5f) return a;
    a.valid = true;
    a.space = Aim::Space::Head;
    a.origin = ctx.headStanding.inverseTransformPoint(raw.origin);
    a.direction = ctx.headStanding.inverseRotate(d);
    return a;
}

Aim EyeGazeSource::readMmap(const FrameContext& ctx) {
    Aim a;
    if (!mmap_.isOpen()) {
        if (ctx.time < nextMmapOpen_) return a;
        nextMmapOpen_ = ctx.time + 2.0;
        if (!mmap_.open()) return a;
    }
    eyepointer::EyeServerSample s;
    EyeServerStatus st = mmap_.read(s);
    if (st != lastStatus_) {
        if (st == EyeServerStatus::UnknownVersion)
            logf("eyes: eye-server.mmap has layout version %u; this build knows %s. Run "
                 "frameeyepointer probe and report the output so it can be added.",
                 s.version, eyepointer::eyeserver::knownVersions().c_str());
        else if (st != EyeServerStatus::Ok)
            logf("eyes: eye-server.mmap: %s", eyepointer::toString(st));
        lastStatus_ = st;
    }
    if (st != EyeServerStatus::Ok) {
        if (st == EyeServerStatus::TooSmall) mmap_.close(); // it may be recreated
        return a;
    }
    if (s.counter != lastCounter_) counterChangedAt_ = ctx.time;
    lastCounter_ = s.counter;
    bool fresh = ctx.time - counterChangedAt_ < 0.2;
    if (!s.producing || !fresh || s.blinking(ctx.settings->blinkOpenness)) return a;
    eyepointer::Vec3 d = s.direction();
    if (eyepointer::length(d) < 0.5f) return a;
    a.valid = true;
    a.space = Aim::Space::Head;
    a.direction = d; // from the head origin
    return a;
}

Aim EyeGazeSource::readAim(const FrameContext& ctx) {
    filter_.setParams(ctx.settings->filter);
    Aim a;
    if (ctx.enabled && ctx.allowed) {
        if (useAction(ctx)) {
            from_ = "action";
            a = readAction(ctx);
        } else if (useMmap(ctx)) {
            from_ = "mmap";
            a = readMmap(ctx);
        } else {
            from_ = "none";
        }
    }
    ++samples_;
    if (a.valid) {
        ++valid_;
        a.direction = filter_.update(a.direction, ctx.time);
        held_ = a;
        heldAt_ = ctx.time;
        return a;
    }
    // Through blinks and dropouts, keep pointing where the eyes last were.
    if (held_.valid && ctx.time - heldAt_ < ctx.settings->holdGazeSeconds) return held_;
    held_.valid = false;
    return {};
}

std::string EyeGazeSource::status() const {
    char buf[96];
    std::snprintf(buf, sizeof buf, "eyes from %s, valid %d/%d", from_, valid_, samples_);
    auto* self = const_cast<EyeGazeSource*>(this);
    self->samples_ = self->valid_ = 0;
    return buf;
}

void EyeGazeSource::probe(const FrameContext& ctx) {
    EyeServerFile f;
    if (!f.open()) {
        logf("eye-server.mmap: can't open %s", EyeServerFile::kPath);
    } else {
        eyepointer::EyeServerSample s;
        EyeServerStatus st = f.read(s);
        logf("eye-server.mmap: %zu bytes, layout version %u (known: %s): %s", f.size(), s.version,
             eyepointer::eyeserver::knownVersions().c_str(), eyepointer::toString(st));
        std::fputs(eyepointer::dumpEyeServer(f.data(), f.size()).c_str(), stdout);
    }

    int actionValid = 0, mmapValid = 0, counterChanges = 0, n = 0;
    Aim lastA, lastM;
    uint32_t lastCounter = 0;
    FrameContext c = ctx;
    for (; n < 90; ++n) {
        if (!ctx.inGame && set_ != vr::k_ulInvalidActionSetHandle) {
            std::vector<vr::VRActiveActionSet_t> sets;
            addActionSet(sets, set_);
            vr::VRInput()->UpdateActionState(sets.data(), sizeof sets[0], 1);
            c.headValid = eyepointer::openvr::hmdPose(0.0f, c.headStanding);
            Aim a = readAction(c);
            if (a.valid) ++actionValid, lastA = a;
        }
        eyepointer::EyeServerSample s;
        if (f.isOpen() && f.read(s) == EyeServerStatus::Ok) {
            if (n > 0 && s.counter != lastCounter) ++counterChanges;
            lastCounter = s.counter;
            eyepointer::Vec3 d = s.direction();
            if (s.producing && eyepointer::length(d) > 0.5f && !s.blinking(ctx.settings->blinkOpenness)) {
                ++mmapValid;
                lastM.valid = true;
                lastM.direction = d;
            }
            if (n == 89)
                logf("mmap last sample: producing %d, age %.0f ms, openness %.2f %.2f, fixation (%.3f, %.3f, %.3f)",
                     s.producing ? 1 : 0, (nowRaw() - s.sampleTime) * 1000.0, s.openness[0], s.openness[1],
                     s.fixation.x, s.fixation.y, s.fixation.z);
        }
        struct timespec ts {0, 33 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
    if (ctx.inGame) logf("gaze action: skipped (a game is running)");
    else logf("gaze action: %d of %d samples valid%s", actionValid, n,
              actionValid ? "" : " (is eye tracking on, and the headset worn?)");
    if (lastA.valid)
        logf("  last: yaw %.1f, pitch %.1f deg (head relative), origin (%.3f, %.3f, %.3f)",
             yawDeg(lastA.direction), pitchDeg(lastA.direction), lastA.origin.x, lastA.origin.y, lastA.origin.z);
    logf("gaze mmap: %d of %d samples valid, counter changed %d times", mmapValid, n, counterChanges);
    if (lastM.valid)
        logf("  last: yaw %.1f, pitch %.1f deg (head relative)", yawDeg(lastM.direction), pitchDeg(lastM.direction));
}

} // namespace frame
