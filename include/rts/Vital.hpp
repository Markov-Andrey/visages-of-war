#pragma once
#include <cmath>
#include <compare>
#include <cstdint>

namespace rts {
// Health and mana use exact hundredths; conversion to whole points is presentation only.
class Vital {
public:
    constexpr Vital() = default;
    constexpr Vital(int points) : hundredths_(std::int64_t(points) * 100) {}
    Vital(double points) : hundredths_(std::llround(points * 100)) {}
    static constexpr Vital fromHundredths(std::int64_t value) { Vital result; result.hundredths_ = value; return result; }
    constexpr std::int64_t hundredths() const { return hundredths_; }
    constexpr explicit operator double() const { return double(hundredths_) / 100; }
    constexpr explicit operator float() const { return float(hundredths_) / 100; }
    std::int64_t rounded() const { return std::llround(double(*this)); }
    auto operator<=>(const Vital&) const = default;
    constexpr Vital& operator+=(Vital value) { hundredths_ += value.hundredths_; return *this; }
    constexpr Vital& operator-=(Vital value) { hundredths_ -= value.hundredths_; return *this; }
    friend constexpr Vital operator+(Vital a, Vital b) { return a += b; }
    friend constexpr Vital operator-(Vital a, Vital b) { return a -= b; }
    friend constexpr Vital operator*(Vital value, int factor) { return fromHundredths(value.hundredths_ * factor); }
    friend constexpr Vital operator*(int factor, Vital value) { return value * factor; }
private:
    std::int64_t hundredths_{};
};
}
