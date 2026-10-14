#include "rts/Tags.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>

namespace rts {
TagCatalog TagCatalog::load(const std::filesystem::path& file) {
    using Json = nlohmann::json;
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Cannot open tag catalog");
    std::vector<std::set<std::string>> keys;
    const auto root = Json::parse(input, [&](int, Json::parse_event_t event, Json& value) {
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
            throw std::runtime_error("Duplicate tag catalog field");
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
    if (!root.is_object() || root.size() != 2 || !root.contains("version") ||
        !root.at("version").is_number_integer() || root.at("version") != 1 ||
        !root.contains("tags") || !root.at("tags").is_array()) throw std::runtime_error("Invalid tag catalog");
    TagCatalog result;
    for (const auto& row : root.at("tags")) {
        if (!row.is_object() || row.size() != 3) throw std::runtime_error("Invalid tag definition");
        TagDefinition tag{row.at("id").get<std::string>(), row.at("name").get<std::string>(), row.at("description").get<std::string>()};
        if (tag.id.empty() || tag.id.size() > 100 || tag.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.-") != std::string::npos ||
            tag.displayName.empty() || tag.displayName.size() > 4096 || tag.description.size() > 4096)
            throw std::runtime_error("Invalid tag ID or text: " + tag.id);
        if (!result.index_.emplace(tag.id, result.tags_.size()).second) throw std::runtime_error("Duplicate tag: " + tag.id);
        result.tags_.push_back(std::move(tag));
    }
    return result;
}
const TagDefinition& TagCatalog::tag(const std::string& id) const {
    const auto found = index_.find(id);
    if (found == index_.end()) throw std::runtime_error("Unknown tag: " + id);
    return tags_[found->second];
}
TagSet TagCatalog::resolve(std::vector<std::string> ids) const {
    for (const auto& id : ids) tag(id);
    return TagSet(std::move(ids));
}
}
