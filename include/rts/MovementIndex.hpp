#pragma once
#include "rts/Types.hpp"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rts {
// Immutable start-of-tick broad phase. Queries include the maximum possible
// displacement this tick, so sequential updates cannot escape the candidate set.
class MovementIndex {
public:
    void clear() { buckets_.clear(); radius_ = step_ = speed_ = 0; }
    void add(size_t index, Vec2 position, float radius, float speed, float step) {
        buckets_[key(bucket(position.x), bucket(position.y))].push_back(index);
        radius_ = std::max(radius_, radius); step_ = std::max(step_, step); speed_ = std::max(speed_, speed);
    }
    float maximumSpeed() const { return speed_; }
    std::vector<size_t> query(Vec2 from, Vec2 to, float radius) const {
        const float extent = groundRadiusExtent(radius + radius_ + step_) + .0001f;
        std::vector<size_t> result;
        for (int y = bucket(std::min(from.y, to.y) - extent); y <= bucket(std::max(from.y, to.y) + extent); ++y)
            for (int x = bucket(std::min(from.x, to.x) - extent); x <= bucket(std::max(from.x, to.x) + extent); ++x)
                if (const auto it = buckets_.find(key(x, y)); it != buckets_.end())
                    result.insert(result.end(), it->second.begin(), it->second.end());
        std::sort(result.begin(), result.end()); // Stable ties, independent of hash iteration.
        return result;
    }
private:
    static int bucket(float coordinate) { return int(std::floor(coordinate / 4)); }
    static std::uint64_t key(int x, int y) { return (std::uint64_t(std::uint32_t(x)) << 32) | std::uint32_t(y); }
    std::unordered_map<std::uint64_t, std::vector<size_t>> buckets_;
    float radius_{}, step_{}, speed_{};
};
}
