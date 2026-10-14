#pragma once
#include <algorithm>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rts {
// Stable catalog IDs, not bit positions: adding tags never changes existing identities.
class TagSet {
public:
    TagSet() = default;
    explicit TagSet(std::vector<std::string> ids) : ids_(std::move(ids)) {
        std::sort(ids_.begin(), ids_.end());
        if ((!ids_.empty() && ids_.front().empty()) || std::adjacent_find(ids_.begin(), ids_.end()) != ids_.end())
            throw std::invalid_argument("Empty or duplicate tag reference");
    }
    const auto& ids() const { return ids_; }
    bool contains(std::string_view id) const { return std::binary_search(ids_.begin(), ids_.end(), id); }
    bool containsAny(std::span<const std::string> ids) const {
        return std::any_of(ids.begin(), ids.end(), [&](const auto& id) { return contains(id); });
    }
    bool containsAll(std::span<const std::string> ids) const {
        return std::all_of(ids.begin(), ids.end(), [&](const auto& id) { return contains(id); });
    }
    bool operator==(const TagSet&) const = default;
private:
    std::vector<std::string> ids_;
};
struct TagDefinition { std::string id, displayName, description; };
class TagCatalog {
public:
    static TagCatalog load(const std::filesystem::path& file);
    const TagDefinition& tag(const std::string& id) const;
    const auto& tags() const { return tags_; }
    TagSet resolve(std::vector<std::string> ids) const;
private:
    std::vector<TagDefinition> tags_;
    std::unordered_map<std::string, size_t> index_;
};
}
