#include "rts/ecs/Systems.hpp"
#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace rts::ecs {
void Systems::add(std::string name, Phase phase, Function function, int priority) {
    if (running_) throw std::logic_error("Cannot register systems during a tick");
    if (name.empty() || !function || phase < Phase::BeforeOrders || phase > Phase::AfterProduction)
        throw std::invalid_argument("Invalid match system registration");
    if (std::any_of(entries_.begin(), entries_.end(), [&](const auto& e) { return e.name == name; }))
        throw std::invalid_argument("Duplicate match system: " + name);
    entries_.push_back({std::move(name), phase, std::move(function), priority});
    std::sort(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
        return std::tie(a.phase, a.priority, a.name) < std::tie(b.phase, b.priority, b.name);
    });
}
bool Systems::run(Phase phase, SystemContext& context) {
    if (running_) throw std::logic_error("Recursive match system execution");
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{running_};
    running_ = true;
    bool invoked = false;
    for (const auto& entry : entries_) if (entry.phase == phase) { entry.function(context); invoked = true; }
    return invoked;
}
}
