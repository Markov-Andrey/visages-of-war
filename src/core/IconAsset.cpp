#include "rts/IconAsset.hpp"
#include <stdexcept>

namespace rts {
IconAsset loadIconAsset(const Paths& paths, const std::filesystem::path& directory) {
    if (directory.empty()) throw std::invalid_argument("Icon directory must not be empty");
    return {paths.asset(directory / L"icon.png"), paths.optionalAsset(directory / L"mask.png")};
}
}
