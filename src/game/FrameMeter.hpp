#pragma once
#include <chrono>
#include <optional>

namespace rts::game {
// Wall-clock intervals between completed frames, including simulation, input,
// rendering, presentation and the main loop's wait. Never use the capped dt.
class FrameMeter {
public:
    using Clock = std::chrono::steady_clock;
    void completedFrame(Clock::time_point now) {
        if (!last_) { last_ = began_ = now; return; }
        if (now <= *last_) return;
        last_ = now;
        ++frames_;
        const auto elapsed = now - began_;
        if (elapsed < std::chrono::milliseconds(500)) return;
        const double seconds = std::chrono::duration<double>(elapsed).count();
        fps_ = frames_ / seconds;
        milliseconds_ = seconds * 1000 / frames_;
        began_ = now;
        frames_ = 0;
    }
    void reset() { *this = {}; }
    double fps() const { return fps_; }
    double milliseconds() const { return milliseconds_; }
private:
    std::optional<Clock::time_point> last_;
    Clock::time_point began_{};
    unsigned frames_{};
    double fps_{}, milliseconds_{};
};
}
