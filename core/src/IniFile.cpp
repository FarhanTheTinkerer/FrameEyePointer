#include "eyepointer/IniFile.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <sstream>

namespace eyepointer {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void warnBad(std::vector<std::string>* w, const std::string& section, const std::string& key,
             const std::string& value, const char* expected) {
    if (w) w->push_back("[" + section + "] " + key + " = '" + value + "': expected " + expected);
}

} // namespace

IniFile IniFile::parse(const std::string& text, std::vector<std::string>* warnings) {
    IniFile ini;
    std::istringstream in(text);
    std::string raw, section;
    int line = 0;
    while (std::getline(in, raw)) {
        ++line;
        std::string l = trim(raw);
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[') {
            if (l.back() != ']') {
                if (warnings) warnings->push_back("line " + std::to_string(line) + ": malformed section");
                continue;
            }
            section = lower(trim(l.substr(1, l.size() - 2)));
            ini.sections_[section];
            continue;
        }
        size_t eq = l.find('=');
        if (eq == std::string::npos) {
            if (warnings) warnings->push_back("line " + std::to_string(line) + ": expected key = value");
            continue;
        }
        std::string key = lower(trim(l.substr(0, eq)));
        std::string value = trim(l.substr(eq + 1));
        for (const char* c : {" ;", "\t;", " #", "\t#"}) {
            size_t p = value.find(c);
            if (p != std::string::npos) value = trim(value.substr(0, p));
        }
        ini.sections_[section][key] = value;
    }
    return ini;
}

bool IniFile::has(const std::string& section, const std::string& key) const {
    auto s = sections_.find(section);
    return s != sections_.end() && s->second.count(key);
}

std::string IniFile::get(const std::string& section, const std::string& key,
                         const std::string& fallback) const {
    auto s = sections_.find(section);
    if (s == sections_.end()) return fallback;
    auto k = s->second.find(key);
    return k == s->second.end() ? fallback : k->second;
}

void IniFile::read(const std::string& section, const std::string& key, bool& value,
                   std::vector<std::string>* warnings) const {
    if (!has(section, key)) return;
    std::string v = lower(get(section, key));
    if (v == "true" || v == "yes" || v == "on" || v == "1") value = true;
    else if (v == "false" || v == "no" || v == "off" || v == "0") value = false;
    else warnBad(warnings, section, key, v, "true or false");
}

void IniFile::read(const std::string& section, const std::string& key, double& value,
                   std::vector<std::string>* warnings, double min, double max) const {
    if (!has(section, key)) return;
    std::string v = get(section, key);
    char* end = nullptr;
    errno = 0;
    double d = std::strtod(v.c_str(), &end);
    if (v.empty() || *end != '\0' || errno != 0 || !(d >= min && d <= max)) {
        std::string range = "a number from " + std::to_string(min) + " to " + std::to_string(max);
        warnBad(warnings, section, key, v, range.c_str());
        return;
    }
    value = d;
}

void IniFile::read(const std::string& section, const std::string& key, float& value,
                   std::vector<std::string>* warnings, float min, float max) const {
    double d = value;
    read(section, key, d, warnings, min, max);
    value = static_cast<float>(d);
}

void IniFile::read(const std::string& section, const std::string& key, int& value,
                   std::vector<std::string>* warnings, int min, int max) const {
    if (!has(section, key)) return;
    std::string v = get(section, key);
    char* end = nullptr;
    errno = 0;
    long n = std::strtol(v.c_str(), &end, 10);
    if (v.empty() || *end != '\0' || errno != 0 || n < min || n > max) {
        std::string range = "a whole number from " + std::to_string(min) + " to " + std::to_string(max);
        warnBad(warnings, section, key, v, range.c_str());
        return;
    }
    value = static_cast<int>(n);
}

} // namespace eyepointer
