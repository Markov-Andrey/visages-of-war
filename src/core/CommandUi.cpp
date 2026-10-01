#include "rts/CommandUi.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace rts {
std::map<std::string, std::filesystem::path> loadCommandIcons(const Paths& paths) {
    std::ifstream file(paths.asset(L"ui/commands.json"));
    const auto catalog = nlohmann::json::parse(file).at("icons");
    if (!catalog.is_object()) throw std::runtime_error("Command icons must be an object");
    std::map<std::string, std::filesystem::path> result;
    for (const auto& [id, value] : catalog.items()) {
        const auto image = value.get<std::string>();
        result.emplace(id, paths.asset(std::filesystem::path(std::u8string(image.begin(), image.end()))));
    }
    for (const auto& command : unitCommands) if (!result.contains(command.icon)) throw std::runtime_error("Missing command icon");
    for (const auto* icon : {"idle-worker", "rally", "cancel"})
        if (!result.contains(icon)) throw std::runtime_error("Missing command icon: " + std::string(icon));
    return result;
}
}
