#include "eyepointer/Reticle.h"

#include <algorithm>
#include <cmath>

namespace eyepointer {

namespace {

/// Coverage of a band [inner, outer] at radius r, with a one-pixel soft edge.
float band(float r, float inner, float outer) {
    float a = std::clamp(r - inner + 0.5f, 0.0f, 1.0f);
    float b = std::clamp(outer - r + 0.5f, 0.0f, 1.0f);
    return std::min(a, b);
}

} // namespace

std::vector<uint8_t> makeReticleImage(int size, uint32_t rgb, float alpha) {
    size = std::max(size, 8);
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4, 0);

    const float c = (size - 1) * 0.5f;
    const float R = size * 0.5f - 1.0f;      // outer edge of the outline
    const float ringOuter = R * 0.82f;
    const float ringInner = R * 0.58f;
    const float dot = R * 0.16f;
    const float outline = std::max(1.0f, R * 0.10f);

    const float cr = ((rgb >> 16) & 0xFF) / 255.0f;
    const float cg = ((rgb >> 8) & 0xFF) / 255.0f;
    const float cb = (rgb & 0xFF) / 255.0f;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float r = std::hypot(x - c, y - c);
            float colour = std::max(band(r, ringInner, ringOuter), band(r, -1.0f, dot));
            float dark = std::max(band(r, ringInner - outline, ringOuter + outline),
                                  band(r, -1.0f, dot + outline));
            // Composite the coloured shapes over a dark outline.
            float a = colour + dark * (1.0f - colour) * 0.6f;
            float k = a > 0 ? colour / a : 0.0f; // share of colour in the result
            uint8_t* p = &px[(static_cast<size_t>(y) * size + x) * 4];
            p[0] = static_cast<uint8_t>(std::lround(cr * k * 255.0f));
            p[1] = static_cast<uint8_t>(std::lround(cg * k * 255.0f));
            p[2] = static_cast<uint8_t>(std::lround(cb * k * 255.0f));
            p[3] = static_cast<uint8_t>(std::lround(a * alpha * 255.0f));
        }
    }
    return px;
}

} // namespace eyepointer
