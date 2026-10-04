#pragma once

#include "eyepointer/Math.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace eyepointer {

/// Where the laser should point, from one input source.
struct Aim {
    enum class Space {
        Head,     ///< relative to the headset (-Z forward); follows the head with no lag (eyes)
        Tracking, ///< SteamVR standing space (a hand ray)
    };
    bool valid = false;
    Space space = Space::Head;
    Vec3 origin;
    Vec3 direction; ///< unit
};

/// What the click inputs say this frame. Several button sources are OR-ed.
struct ButtonState {
    bool click = false;
    bool rightClick = false;
    bool toggle = false;
    float scroll = 0; ///< -1..1, the largest magnitude wins
    void merge(const ButtonState& o);
};

/// Picks the aim from the first source in `priority` that has a valid aim.
/// Returns the chosen source's name, or nothing.
std::optional<std::string> selectAim(const std::vector<std::string>& priority,
                                     const std::map<std::string, Aim>& aims);

/// Splits "a, b,c" into {"a", "b", "c"} (lower-cased, empty items dropped).
std::vector<std::string> splitList(const std::string& text);

/// External input protocol: one text message per datagram on the abstract unix
/// socket "@frameeyepointer_input". Any process can be an input source this way
/// (a hand tracker, a test script). Messages:
///
///   aim <source> head|tracking <ox> <oy> <oz> <dx> <dy> <dz>
///   aim <source> none                     the source stops pointing
///   btn <source> click|rightclick|toggle <0|1>
///   scroll <source> <y>                   -1..1
///
/// <source> names the provider (e.g. "hands"); settings refer to it by that name.
/// A source that sends nothing for [external] timeout_ms is treated as gone.
struct InputMessage {
    enum class Type { Aim, Button, Scroll };
    Type type = Type::Aim;
    std::string source;
    Aim aim;               ///< Type::Aim (valid=false for "none")
    std::string button;    ///< Type::Button: click, rightclick, toggle
    bool down = false;     ///< Type::Button
    float scroll = 0;      ///< Type::Scroll
};

/// Parses one message; nothing if it is malformed.
std::optional<InputMessage> parseInputMessage(const std::string& text);

} // namespace eyepointer
