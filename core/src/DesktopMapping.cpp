#include "eyepointer/DesktopMapping.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace eyepointer {

namespace {

bool sameSize(const Rect& r, float w, float h) {
    return std::fabs(r.width - w) <= 2.0f && std::fabs(r.height - h) <= 2.0f;
}

bool sameAspect(const Rect& r, float w, float h) {
    if (r.height <= 0 || h <= 0) return false;
    float a = static_cast<float>(r.width) / static_cast<float>(r.height);
    float b = w / h;
    return std::fabs(a - b) / b < 0.01f;
}

const Monitor* primaryMonitor(const std::vector<Monitor>& monitors) {
    for (const auto& m : monitors)
        if (m.primary) return &m;
    return monitors.empty() ? nullptr : &monitors.front();
}

} // namespace

std::optional<DesktopSelector> DesktopSelector::parse(const std::string& text) {
    DesktopSelector s;
    if (text == "auto") return s;
    if (text == "virtual") {
        s.kind = Kind::VirtualScreen;
        return s;
    }
    if (text == "primary") {
        s.kind = Kind::Primary;
        return s;
    }
    int a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (std::sscanf(text.c_str(), "monitor:%d%c", &a, &tail) == 1 && a >= 0) {
        s.kind = Kind::MonitorIndex;
        s.monitorIndex = a;
        return s;
    }
    if (std::sscanf(text.c_str(), "rect:%d,%d,%d,%d%c", &a, &b, &c, &d, &tail) == 4 && c > 0 &&
        d > 0) {
        s.kind = Kind::Explicit;
        s.rect = {a, b, c, d};
        return s;
    }
    return std::nullopt;
}

Rect virtualScreen(const std::vector<Monitor>& monitors) {
    if (monitors.empty()) return {};
    int x0 = monitors[0].rect.x, y0 = monitors[0].rect.y;
    int x1 = x0 + monitors[0].rect.width, y1 = y0 + monitors[0].rect.height;
    for (const auto& m : monitors) {
        x0 = std::min(x0, m.rect.x);
        y0 = std::min(y0, m.rect.y);
        x1 = std::max(x1, m.rect.x + m.rect.width);
        y1 = std::max(y1, m.rect.y + m.rect.height);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

std::optional<Rect> chooseDesktopRect(const DesktopSelector& selector, float textureWidth,
                                      float textureHeight, const std::vector<Monitor>& monitors) {
    switch (selector.kind) {
    case DesktopSelector::Kind::Explicit:
        return selector.rect;
    case DesktopSelector::Kind::VirtualScreen:
        if (monitors.empty()) return std::nullopt;
        return virtualScreen(monitors);
    case DesktopSelector::Kind::Primary:
        if (const Monitor* p = primaryMonitor(monitors)) return p->rect;
        return std::nullopt;
    case DesktopSelector::Kind::MonitorIndex:
        if (selector.monitorIndex < static_cast<int>(monitors.size()))
            return monitors[selector.monitorIndex].rect;
        return std::nullopt;
    case DesktopSelector::Kind::Auto:
        break;
    }

    if (monitors.empty() || textureWidth <= 0 || textureHeight <= 0) return std::nullopt;
    Rect all = virtualScreen(monitors);
    const Monitor* primary = primaryMonitor(monitors);

    if (sameSize(all, textureWidth, textureHeight)) return all;
    if (primary && sameSize(primary->rect, textureWidth, textureHeight)) return primary->rect;
    const Monitor* match = nullptr;
    int matches = 0;
    for (const auto& m : monitors) {
        if (sameSize(m.rect, textureWidth, textureHeight)) {
            match = &m;
            ++matches;
        }
    }
    if (matches == 1) return match->rect;

    // The capture may be downscaled; fall back to the aspect ratio.
    if (sameAspect(all, textureWidth, textureHeight)) return all;
    if (primary && sameAspect(primary->rect, textureWidth, textureHeight)) return primary->rect;
    return std::nullopt;
}

std::optional<Point> uvToDesktopPixel(float u, float v, const Rect& rect,
                                      const std::vector<Monitor>& monitors) {
    if (!(u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f)) return std::nullopt;
    if (rect.width <= 0 || rect.height <= 0) return std::nullopt;
    int px = rect.x + std::min(rect.width - 1, static_cast<int>(std::floor(u * rect.width)));
    int py = rect.y +
             std::min(rect.height - 1, static_cast<int>(std::floor((1.0f - v) * rect.height)));
    if (!monitors.empty()) {
        bool onScreen = std::any_of(monitors.begin(), monitors.end(),
                                    [&](const Monitor& m) { return m.rect.contains(px, py); });
        if (!onScreen) return std::nullopt;
    }
    return Point{px, py};
}

} // namespace eyepointer
