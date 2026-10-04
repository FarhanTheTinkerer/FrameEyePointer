#pragma once

#include <cstdint>
#include <vector>

namespace eyepointer {

/// Draws the pointer image: an anti-aliased ring with a centre dot and a dark
/// outline so it reads on light and dark backgrounds. Returns `size * size`
/// RGBA8 pixels (straight alpha), row-major from the top.
std::vector<uint8_t> makeReticleImage(int size, uint32_t rgb, float alpha);

} // namespace eyepointer
