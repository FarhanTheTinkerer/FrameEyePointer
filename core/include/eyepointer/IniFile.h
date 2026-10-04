#pragma once

#include <map>
#include <string>
#include <vector>

namespace eyepointer {

/// Minimal INI reader: [section], key = value, ';' or '#' comments (also after a
/// value, following whitespace). Section and key names are lower-cased.
class IniFile {
public:
    static IniFile parse(const std::string& text, std::vector<std::string>* warnings = nullptr);

    bool has(const std::string& section, const std::string& key) const;
    std::string get(const std::string& section, const std::string& key,
                    const std::string& fallback = {}) const;

    /// Typed getters: keep `value` and add a warning if the entry is present but malformed.
    void read(const std::string& section, const std::string& key, bool& value,
              std::vector<std::string>* warnings) const;
    void read(const std::string& section, const std::string& key, float& value,
              std::vector<std::string>* warnings, float min = -1e30f, float max = 1e30f) const;
    void read(const std::string& section, const std::string& key, double& value,
              std::vector<std::string>* warnings, double min = -1e300, double max = 1e300) const;
    void read(const std::string& section, const std::string& key, int& value,
              std::vector<std::string>* warnings, int min = -2147483647, int max = 2147483647) const;

    const std::map<std::string, std::map<std::string, std::string>>& sections() const {
        return sections_;
    }

private:
    std::map<std::string, std::map<std::string, std::string>> sections_;
};

} // namespace eyepointer
