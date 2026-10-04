#pragma once

#include "InputSource.h"

#include "eyepointer/EyeServer.h"
#include "eyepointer/GazeFilter.h"

namespace frame {

/// /dev/shm/eye-server.mmap, mapped read-only.
class EyeServerFile {
public:
    static constexpr const char* kPath = "/dev/shm/eye-server.mmap";
    ~EyeServerFile() { close(); }
    bool open();
    void close();
    bool isOpen() const { return data_ != nullptr; }
    size_t size() const { return size_; }
    const volatile uint8_t* data() const { return data_; }
    /// Retries reads that raced the writer.
    eyepointer::EyeServerStatus read(eyepointer::EyeServerSample& s) const;

private:
    const volatile uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

/// "eyes": the headset's eye tracking as a head-relative aim.
///
/// SteamVR's eyetracking action outside games; during games the read-only shared
/// memory instead, because reading the action in a game makes SteamVR restart its
/// eye tracker (found by Frametop). Smooths the direction with a One Euro filter
/// and holds the last gaze through blinks for [pointer] hold_gaze_seconds.
class EyeGazeSource : public InputSource {
public:
    /// After SetActionManifestPath.
    EyeGazeSource();

    std::string name() const override { return "eyes"; }
    void collectActionSets(const FrameContext& ctx, std::vector<vr::VRActiveActionSet_t>& sets) override;
    bool providesAim() const override { return true; }
    eyepointer::Aim readAim(const FrameContext& ctx) override;
    std::string status() const override;
    void probe(const FrameContext& ctx) override;

private:
    bool useAction(const FrameContext& ctx) const;
    bool useMmap(const FrameContext& ctx) const;
    eyepointer::Aim readAction(const FrameContext& ctx);
    eyepointer::Aim readMmap(const FrameContext& ctx);

    vr::VRActionSetHandle_t set_ = vr::k_ulInvalidActionSetHandle;
    vr::VRActionHandle_t action_ = vr::k_ulInvalidActionHandle;
    eyepointer::GazeFilter filter_;
    EyeServerFile mmap_;
    double nextMmapOpen_ = 0;
    uint32_t lastCounter_ = 0;
    double counterChangedAt_ = -1e9;
    eyepointer::EyeServerStatus lastStatus_ = eyepointer::EyeServerStatus::Ok;
    bool warnedAction_ = false;

    eyepointer::Aim held_;
    double heldAt_ = -1e9;
    const char* from_ = "none";
    int samples_ = 0, valid_ = 0;
};

} // namespace frame
