#include "eyepointer/InputSources.h"

#include <cctype>
#include <cmath>
#include <sstream>

namespace eyepointer {

void ButtonState::merge(const ButtonState& o) {
    click = click || o.click;
    rightClick = rightClick || o.rightClick;
    toggle = toggle || o.toggle;
    if (std::fabs(o.scroll) > std::fabs(scroll)) scroll = o.scroll;
}

std::optional<std::string> selectAim(const std::vector<std::string>& priority,
                                     const std::map<std::string, Aim>& aims) {
    for (const auto& name : priority) {
        auto it = aims.find(name);
        if (it != aims.end() && it->second.valid) return name;
    }
    return std::nullopt;
}

std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> out;
    std::string item;
    auto flush = [&] {
        if (!item.empty()) out.push_back(item);
        item.clear();
    };
    for (char c : text) {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) flush();
        else item += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    flush();
    return out;
}

namespace {

bool validName(const std::string& s) {
    if (s.empty() || s.size() > 32) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) return false;
    return true;
}

} // namespace

std::optional<InputMessage> parseInputMessage(const std::string& text) {
    std::istringstream in(text);
    std::string kind;
    InputMessage m;
    if (!(in >> kind >> m.source) || !validName(m.source)) return std::nullopt;
    for (char& c : m.source) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string rest;

    if (kind == "aim") {
        m.type = InputMessage::Type::Aim;
        std::string space;
        if (!(in >> space)) return std::nullopt;
        if (space == "none") {
            if (in >> rest) return std::nullopt;
            return m;
        }
        if (space == "head") m.aim.space = Aim::Space::Head;
        else if (space == "tracking") m.aim.space = Aim::Space::Tracking;
        else return std::nullopt;
        float v[6];
        for (float& f : v)
            if (!(in >> f) || !std::isfinite(f)) return std::nullopt;
        if (in >> rest) return std::nullopt;
        Vec3 d = normalize({v[3], v[4], v[5]});
        if (length(d) < 0.5f) return std::nullopt;
        m.aim.valid = true;
        m.aim.origin = {v[0], v[1], v[2]};
        m.aim.direction = d;
        return m;
    }
    if (kind == "btn") {
        m.type = InputMessage::Type::Button;
        int down = 0;
        if (!(in >> m.button >> down) || (down != 0 && down != 1)) return std::nullopt;
        if (m.button != "click" && m.button != "rightclick" && m.button != "toggle") return std::nullopt;
        if (in >> rest) return std::nullopt;
        m.down = down == 1;
        return m;
    }
    if (kind == "scroll") {
        m.type = InputMessage::Type::Scroll;
        if (!(in >> m.scroll) || !std::isfinite(m.scroll)) return std::nullopt;
        if (in >> rest) return std::nullopt;
        m.scroll = std::fmax(-1.0f, std::fmin(1.0f, m.scroll));
        return m;
    }
    return std::nullopt;
}

} // namespace eyepointer
