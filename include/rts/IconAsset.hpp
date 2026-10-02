#pragma once
#include "rts/Paths.hpp"

namespace rts {
struct IconAsset { std::filesystem::path image, mask; };
// An asset-relative directory containing icon.png and an optional mask.png.
// Resolve once when loading resources; rendering uses the resolved paths/cache.
IconAsset loadIconAsset(const Paths& paths, const std::filesystem::path& directory);
}
