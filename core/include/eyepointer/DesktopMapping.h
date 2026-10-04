#pragma once

#include <optional>
#include <string>
#include <vector>

namespace eyepointer {

/// A rectangle in Windows virtual-desktop physical pixels (x, y may be negative).
struct Rect {
    int x = 0, y = 0, width = 0, height = 0;

    bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
    bool operator==(const Rect& o) const {
        return x == o.x && y == o.y && width == o.width && height == o.height;
    }
};

struct Monitor {
    Rect rect;
    bool primary = false;
};

struct Point {
    int x = 0, y = 0;
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

/// Which part of the Windows desktop an overlay shows.
struct DesktopSelector {
    enum class Kind { Auto, VirtualScreen, Primary, MonitorIndex, Explicit };
    Kind kind = Kind::Auto;
    int monitorIndex = 0;
    Rect rect;

    /// Parses "auto", "virtual", "primary", "monitor:N" or "rect:x,y,w,h".
    static std::optional<DesktopSelector> parse(const std::string& text);
};

/// Bounding box of all monitors.
Rect virtualScreen(const std::vector<Monitor>& monitors);

/// Picks the desktop rectangle an overlay with a `textureWidth` x `textureHeight`
/// texture (its mouse scale) most likely shows. In Auto mode an exact size match
/// wins (whole virtual desktop first, then the primary monitor, then a uniquely
/// sized monitor), then a matching aspect ratio. Returns nothing if the overlay
/// does not look like a desktop.
std::optional<Rect> chooseDesktopRect(const DesktopSelector& selector, float textureWidth,
                                      float textureHeight, const std::vector<Monitor>& monitors);

/// Converts an OpenVR overlay intersection UV (origin bottom-left, v up) to a
/// desktop pixel inside `rect`. Returns nothing when the UV is outside [0,1] or
/// lands in a gap between monitors.
std::optional<Point> uvToDesktopPixel(float u, float v, const Rect& rect,
                                      const std::vector<Monitor>& monitors);

} // namespace eyepointer
