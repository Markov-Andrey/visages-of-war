#pragma once
#include "rts/FogOfWar.hpp"
#include "rts/Match.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>
#include <utility>

namespace rts {
struct FloatingNotification {
    Vec2 position;
    float height{};
    std::wstring text;
    std::string resourceIcon;
    unsigned color = 0xffa044;
    // A recipient sees private feedback. No recipient means currently visible observers only.
    std::optional<PlayerId> recipient;
    std::uint64_t started{};
    int durationTicks = 2 * simulationTicksPerSecond;
    bool visibleTo(PlayerId viewer, const FogOfWar& fog) const {
        return recipient ? *recipient == viewer : fog.visible({int(std::floor(position.x)), int(std::floor(position.y))});
    }
    float progress(std::uint64_t now) const {
        return now < started ? 0 : std::min(1.f, float(now - started) / std::max(1, durationTicks));
    }
    float opacity(std::uint64_t now) const {
        if (now < started) return 0;
        return std::clamp((1 - progress(now)) * 2, 0.f, 1.f);
    }
};

// Per-viewer presentation queue. Never backlog unseen events for later exploration.
class FloatingNotifications {
public:
    void publish(FloatingNotification notification, PlayerId viewer, const FogOfWar& fog) {
        if (notification.durationTicks <= 0 || notification.text.empty() || !notification.visibleTo(viewer, fog)) return;
        expire(notification.started);
        if (entries_.size() == maximumEntries) entries_.erase(entries_.begin());
        entries_.push_back(std::move(notification));
    }
    void expire(std::uint64_t now) {
        std::erase_if(entries_, [now](const auto& entry) { return entry.progress(now) >= 1; });
    }
    const std::vector<FloatingNotification>& entries() const { return entries_; }
private:
    static constexpr size_t maximumEntries = 256;
    std::vector<FloatingNotification> entries_;
};
}
