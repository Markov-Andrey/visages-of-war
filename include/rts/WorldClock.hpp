#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace rts {
enum class DayPhase { Day, Night };
struct DayPhaseChanged { DayPhase before; DayPhase after; int minute; };
struct ClockSettings {
    int ticksPerSecond = 30;
    int cycleSeconds = 480;
    int startMinute = 22 * 60;
    int sunriseMinute = 6 * 60;
    int sunsetMinute = 18 * 60;
};
class WorldClock {
public:
    explicit WorldClock(ClockSettings settings = {}) : settings_(settings) {
        if (settings.ticksPerSecond <= 0 || settings.ticksPerSecond > 1000 ||
            settings.cycleSeconds <= 0 || settings.cycleSeconds > 86400 ||
            settings.cycleSeconds * settings.ticksPerSecond < 1440 ||
            settings.startMinute < 0 || settings.startMinute >= 1440 ||
            settings.sunriseMinute < 0 || settings.sunsetMinute >= 1440 ||
            settings.sunriseMinute >= settings.sunsetMinute)
            throw std::invalid_argument("Invalid day/night cycle settings");
        cycleTicks_ = static_cast<std::uint64_t>(settings.ticksPerSecond) * settings.cycleSeconds;
    }
    double minuteOfDay() const {
        const double minute = settings_.startMinute + static_cast<double>(tickInCycle_) * 1440.0 / cycleTicks_;
        return minute >= 1440.0 ? minute - 1440.0 : minute;
    }
    DayPhase phase() const {
        const double minute = minuteOfDay();
        return minute >= settings_.sunriseMinute && minute < settings_.sunsetMinute ? DayPhase::Day : DayPhase::Night;
    }
    std::uint64_t elapsedTicks() const { return elapsedTicks_; }
    std::optional<DayPhaseChanged> tick() {
        const auto before = phase();
        tickInCycle_ = (tickInCycle_ + 1) % cycleTicks_;
        ++elapsedTicks_;
        const auto after = phase();
        if (before == after) return std::nullopt;
        return DayPhaseChanged{before, after, static_cast<int>(minuteOfDay())};
    }
private:
    ClockSettings settings_;
    std::uint64_t cycleTicks_{};
    std::uint64_t tickInCycle_{};
    std::uint64_t elapsedTicks_{};
};
}
