#pragma once
#include "rts/Definitions.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace rts::data {
using Json = nlohmann::json;
inline void fields(const Json& value, std::initializer_list<const char*> names) {
    if (!value.is_object()) throw std::runtime_error("Expected an object");
    for (const char* name : names) if (!value.contains(name)) throw std::runtime_error(std::string("Missing field: ") + name);
    for (const auto& item : value.items())
        if (std::none_of(names.begin(), names.end(), [&](const char* name) { return item.key() == name; }))
            throw std::runtime_error("Unknown field: " + item.key());
}
inline int number(const Json& value, int low, int high) {
    if (!value.is_number_integer() || value.get<double>() < low || value.get<double>() > high)
        throw std::runtime_error("Integer outside allowed range");
    return value.get<int>();
}
inline float real(const Json& value, float low, float high) {
    if (!value.is_number()) throw std::runtime_error("Expected a number");
    const float result = value.get<float>();
    if (!std::isfinite(result) || result < low || result > high) throw std::runtime_error("Number outside allowed range");
    return result;
}
inline std::string string(const Json& value, bool allowEmpty = false) {
    auto result = value.get<std::string>();
    if ((!allowEmpty && result.empty()) || result.size() > 4096) throw std::runtime_error("Invalid text field");
    return result;
}
void parseWeapon(EntityDefinition& entity, const Json& attack, const Json& sprite);
}
