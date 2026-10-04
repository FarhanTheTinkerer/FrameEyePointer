#include "eyepointer/EyeServer.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace eyepointer {

namespace eyeserver {

namespace {
const Layout kLayouts[] = {
    {4, 0x152, 0x4f21a, "SteamVR eye-server 0.5.0, build 20260921"},
    {5, 0x157, 0x4f21f, "build 20260930"},
};
} // namespace

const Layout* findLayout(uint32_t version) {
    for (const auto& l : kLayouts)
        if (l.version == version) return &l;
    return nullptr;
}

std::string knownVersions() {
    std::string s;
    for (const auto& l : kLayouts) s += (s.empty() ? "" : ", ") + std::to_string(l.version);
    return s;
}

} // namespace eyeserver

namespace {

void copyOut(const volatile uint8_t* data, size_t offset, void* dst, size_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < n; ++i) d[i] = data[offset + i];
}

template <typename T> T readAt(const volatile uint8_t* data, size_t offset) {
    T v;
    copyOut(data, offset, &v, sizeof v);
    return v;
}

Vec3 vecAt(const uint8_t* record, size_t offset) {
    float f[3];
    std::memcpy(f, record + offset, sizeof f);
    return {f[0], f[1], f[2]};
}

} // namespace

Vec3 EyeServerSample::direction() const {
    // The fixation point is where both eyes converge; it gives one ray from the head
    // origin. Fall back to the mean of the eye directions if it is degenerate.
    if (isFinite(fixation) && length(fixation) > 0.05f && fixation.z < 0) return normalize(fixation);
    Vec3 sum = left + right;
    if (isFinite(sum) && length(sum) > 0.5f) return normalize(sum);
    return {};
}

const char* toString(EyeServerStatus s) {
    switch (s) {
    case EyeServerStatus::Ok: return "ok";
    case EyeServerStatus::TooSmall: return "file too small";
    case EyeServerStatus::NotInitialized: return "not initialized";
    case EyeServerStatus::UnknownVersion: return "unknown layout version";
    case EyeServerStatus::Torn: return "changed while reading";
    }
    return "?";
}

EyeServerStatus parseEyeServer(const volatile uint8_t* data, size_t size, EyeServerSample& out) {
    using namespace eyeserver;
    if (size < kCounterOffset + 4) return EyeServerStatus::TooSmall;
    out.version = readAt<uint32_t>(data, kVersionOffset);
    if (readAt<uint32_t>(data, kInitializedOffset) != 1) return EyeServerStatus::NotInitialized;
    const Layout* layout = findLayout(out.version);
    if (!layout) return EyeServerStatus::UnknownVersion;
    if (size < layout->minSize) return EyeServerStatus::TooSmall;

    uint8_t record[0x81];
    uint32_t before = readAt<uint32_t>(data, kCounterOffset);
    std::atomic_thread_fence(std::memory_order_acquire);
    copyOut(data, layout->recordOffset, record, sizeof record);
    std::atomic_thread_fence(std::memory_order_acquire);
    uint32_t after = readAt<uint32_t>(data, kCounterOffset);
    if (before != after) return EyeServerStatus::Torn;

    uint32_t producer;
    std::memcpy(&producer, record + kProducerState, 4);
    out.counter = after;
    out.producing = producer == 1;
    std::memcpy(&out.sampleTime, record + kSampleTime, 8);
    out.left = vecAt(record, kGaze);
    out.right = vecAt(record, kGaze + 12);
    out.fixation = vecAt(record, kFixation);
    std::memcpy(out.openness, record + kOpenness, sizeof out.openness);
    return EyeServerStatus::Ok;
}

std::string dumpEyeServer(const volatile uint8_t* data, size_t size) {
    std::string out;
    char line[160];
    auto dump = [&](size_t from, size_t to) {
        for (size_t row = from; row < to && row < size; row += 16) {
            int n = std::snprintf(line, sizeof line, "%06zx:", row);
            for (size_t i = row; i < row + 16 && i < size; ++i)
                n += std::snprintf(line + n, sizeof line - n, " %02x", static_cast<unsigned>(data[i]));
            out += line;
            out += '\n';
        }
    };
    std::snprintf(line, sizeof line, "size %zu bytes\n", size);
    out += line;
    dump(0x00, 0x40);
    out += "...\n";
    dump(0x140, 0x220);
    return out;
}

} // namespace eyepointer
