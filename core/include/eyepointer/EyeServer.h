#pragma once

#include "eyepointer/Math.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace eyepointer {

/// Steam Frame eye tracker shared memory, /dev/shm/eye-server.mmap.
///
/// Undocumented: SteamVR's eyetracking process writes it for the headset driver.
/// The layout comes from konsti219/frameeyeosc (versions 4 and 5) and
/// DeeJanuz/frametop (consistency check, field meanings). It changes between
/// SteamVR builds, so each known version is listed and anything else is refused.
/// Only ever read it: the file also carries calibration data back to the tracker.
namespace eyeserver {

constexpr size_t kVersionOffset = 0x00;
constexpr size_t kInitializedOffset = 0x04;
constexpr size_t kCounterOffset = 0x38; ///< u32, changes with every sample

struct Layout {
    uint32_t version;
    size_t recordOffset; ///< start of the packed eye record
    size_t minSize;      ///< smallest valid file size
    const char* build;   ///< where it was seen
};

/// Known layouts, or nullptr.
const Layout* findLayout(uint32_t version);

/// The versions this build understands, for messages ("4, 5").
std::string knownVersions();

// Offsets inside the packed record (same for every known version so far).
constexpr size_t kProducerState = 0x00; ///< u32, 1 while the tracker produces
constexpr size_t kSampleTime = 0x05;    ///< f64, CLOCK_MONOTONIC_RAW seconds
constexpr size_t kGaze = 0x0d;          ///< 2 x vec3: left, right unit directions (head space, -Z forward)
constexpr size_t kFixation = 0x3d;      ///< vec3: fixation point (head space, metres)
constexpr size_t kOpenness = 0x79;      ///< 2 x f32: eye openness, left, right

} // namespace eyeserver

struct EyeServerSample {
    uint32_t version = 0;
    uint32_t counter = 0;
    double sampleTime = 0;   ///< CLOCK_MONOTONIC_RAW seconds
    bool producing = false;  ///< tracker running (producer state 1)
    Vec3 left, right;        ///< per-eye unit directions, head space
    Vec3 fixation;           ///< fixation point, head space
    float openness[2] = {0, 0};

    /// Combined gaze direction in head space (-Z forward), or a zero vector if unusable.
    Vec3 direction() const;
    /// Both eyes look closed (a blink): the direction is not meaningful.
    bool blinking(float threshold) const {
        return openness[0] < threshold && openness[1] < threshold;
    }
};

enum class EyeServerStatus { Ok, TooSmall, NotInitialized, UnknownVersion, Torn };

const char* toString(EyeServerStatus s);

/// Parses one snapshot of the file. `data` may be changing underneath (it is a
/// live mapping), so values are copied out and the counter is checked before and
/// after; a changed counter returns Torn and the caller retries.
EyeServerStatus parseEyeServer(const volatile uint8_t* data, size_t size, EyeServerSample& out);

/// Hex dump of the header and the region around the record, for --probe reports.
std::string dumpEyeServer(const volatile uint8_t* data, size_t size);

} // namespace eyepointer
