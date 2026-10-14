#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rts {
struct ArmorRules {
    double coefficient = .06;
    void validate() const {
        if (!std::isfinite(coefficient) || coefficient <= 0 || coefficient > 1)
            throw std::invalid_argument("Armor coefficient must be finite and in (0, 1]");
    }
    double damageMultiplier(int armor) const {
        const double scaled = coefficient * double(armor);
        return armor >= 0 ? 1.0 / (1.0 + scaled) : 1.0 - scaled;
    }
    int damage(int incoming, int armor) const {
        if (incoming <= 0) return 0;
        const double rounded = std::round(double(incoming) * damageMultiplier(armor));
        return static_cast<int>(std::clamp(rounded, 1.0, double(std::numeric_limits<int>::max())));
    }
};
}
