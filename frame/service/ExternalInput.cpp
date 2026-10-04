#include "ExternalInput.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace frame {

ExternalInputHub::ExternalInputHub() {
    fd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const size_t n = std::strlen(kSocketName);
    std::memcpy(addr.sun_path + 1, kSocketName, n); // abstract namespace
    auto len = socklen_t(offsetof(sockaddr_un, sun_path) + 1 + n);
    if (fd_ < 0 || bind(fd_, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
        logf("external inputs: can't listen on @%s (another frameeyepointerd running?)", kSocketName);
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
    }
}

ExternalInputHub::~ExternalInputHub() {
    if (fd_ >= 0) close(fd_);
}

void ExternalInputHub::poll(double time) {
    if (fd_ < 0) return;
    char buf[512];
    for (int i = 0; i < 256; ++i) { // bounded: never stall the frame
        ssize_t got = recv(fd_, buf, sizeof buf - 1, 0);
        if (got <= 0) break;
        buf[got] = 0;
        auto m = eyepointer::parseInputMessage(buf);
        if (!m) {
            if (rejected_++ < 5) logf("external inputs: ignored malformed message '%.80s'", buf);
            continue;
        }
        State& s = sources_[m->source];
        if (s.lastSeen < -1e8) logf("external inputs: source '%s' connected", m->source.c_str());
        s.lastSeen = time;
        switch (m->type) {
        case eyepointer::InputMessage::Type::Aim:
            s.aim = m->aim;
            s.aimAt = time;
            break;
        case eyepointer::InputMessage::Type::Button:
            if (m->button == "click") s.buttons.click = m->down;
            else if (m->button == "rightclick") s.buttons.rightClick = m->down;
            else if (m->button == "toggle") s.buttons.toggle = m->down;
            break;
        case eyepointer::InputMessage::Type::Scroll:
            s.buttons.scroll = m->scroll;
            break;
        }
    }
}

eyepointer::Aim ExternalInputHub::aim(const std::string& source, double time, double timeout) const {
    auto it = sources_.find(source);
    if (it == sources_.end() || time - it->second.aimAt > timeout) return {};
    return it->second.aim;
}

eyepointer::ButtonState ExternalInputHub::buttons(const std::string& source, double time, double timeout) const {
    auto it = sources_.find(source);
    if (it == sources_.end() || time - it->second.lastSeen > timeout) return {};
    return it->second.buttons;
}

double ExternalInputHub::silentFor(const std::string& source, double time) const {
    auto it = sources_.find(source);
    return it == sources_.end() ? -1.0 : time - it->second.lastSeen;
}

eyepointer::Aim ExternalSource::readAim(const FrameContext& ctx) {
    lastTime_ = ctx.time;
    if (!ctx.enabled || !ctx.allowed) return {};
    return hub_.aim(name_, ctx.time, ctx.settings->externalTimeoutSeconds);
}

eyepointer::ButtonState ExternalSource::readButtons(const FrameContext& ctx) {
    lastTime_ = ctx.time;
    eyepointer::ButtonState b = hub_.buttons(name_, ctx.time, ctx.settings->externalTimeoutSeconds);
    if (!ctx.enabled || !ctx.allowed) {
        b.click = b.rightClick = false;
        b.scroll = 0;
    }
    return b;
}

std::string ExternalSource::status() const {
    double silent = hub_.silentFor(name_, lastTime_);
    char buf[96];
    if (silent < 0) std::snprintf(buf, sizeof buf, "%s: never connected", name_.c_str());
    else std::snprintf(buf, sizeof buf, "%s: last message %.1f s ago", name_.c_str(), silent);
    return buf;
}

void ExternalSource::probe(const FrameContext& ctx) {
    logf("external source '%s': %s (socket @%s %s)", name_.c_str(),
         hub_.silentFor(name_, ctx.time) < 0 ? "no messages yet" : "has sent messages",
         ExternalInputHub::kSocketName, hub_.listening() ? "open" : "NOT open");
}

} // namespace frame
