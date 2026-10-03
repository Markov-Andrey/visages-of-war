#include "rts/DirectionalSprite.hpp"
#include "core/DefinitionData.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace rts {
namespace {
void validate(const SpritePixels& image) {
    if (image.width <= 0 || image.height <= 0 || image.width > 8192 || image.height > 8192 ||
        image.bgra.size() != size_t(image.width) * image.height * 4)
        throw std::invalid_argument("Invalid directional sprite pixels");
}
void validate(const SpriteWarp& warp) {
    for (size_t i = 0; i < warp.width.size(); ++i)
        if (!std::isfinite(warp.width[i]) || warp.width[i] < .7f || warp.width[i] > 1.2f ||
            !std::isfinite(warp.shift[i]) || std::abs(warp.shift[i]) > .08f)
            throw std::invalid_argument("Directional warp exceeds conservative distortion limits");
    if (warp.width.back() != 1 || warp.shift.back() != 0)
        throw std::invalid_argument("Directional warp must preserve the ground pivot");
}
std::filesystem::path utf8Path(const std::string& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
}
DirectionalSpriteRecipe loadDirectionalSpriteRecipe(const Paths& paths, const std::string& name) {
    using namespace data;
    std::ifstream input(paths.asset(utf8Path(name)));
    const auto j = Json::parse(input);
    fields(j, {"version", "frameSize", "pivot", "se", "nw", "warp"});
    number(j.at("version"), 1, 1);
    const auto pair = [](const Json& value) {
        if (!value.is_array() || value.size() != 2) throw std::runtime_error("Expected sprite coordinate pair");
        return Vec2{real(value[0], 0, 8192), real(value[1], 0, 8192)};
    };
    const auto& size = j.at("frameSize");
    pair(size);
    DirectionalSpriteRecipe result;
    result.width = number(size[0], 16, 512); result.height = number(size[1], 16, 512);
    result.pivot = pair(j.at("pivot"));
    if (result.pivot.x != result.width * .5f || result.pivot.y <= result.height * .8f || result.pivot.y >= result.height)
        throw std::runtime_error("Directional sprite needs a centered, grounded pivot");
    size_t direction = 0;
    for (const auto key : {"se", "nw"}) {
        const auto& source = j.at(key);
        fields(source, {"stand", "walk", "attack"});
        size_t column = 0;
        for (const auto animation : {"stand", "walk", "attack"}) {
            const auto& sequence = source.at(animation);
            const size_t count = std::string_view(animation) == "stand" ? 1 : 4;
            if (!sequence.is_array() || sequence.size() != count) throw std::runtime_error("Expected stand + four walk + four attack frames");
            for (const auto& item : sequence) {
                fields(item, {"image", "source", "pivot", "scale"});
                auto& frame = result.sources[direction][column++];
                frame.image = string(item.at("image"));
                paths.asset(utf8Path(frame.image)); // Includes traversal and Unicode validation.
                const auto& rect = item.at("source");
                if (!rect.is_array() || rect.size() != 4) throw std::runtime_error("Expected sprite source rectangle");
                for (size_t i = 0; i < 4; ++i) frame.source[i] = number(rect[i], i < 2 ? 0 : 1, 8192);
                frame.pivot = pair(item.at("pivot"));
                if (frame.pivot.x < frame.source[0] || frame.pivot.x > frame.source[0] + frame.source[2] ||
                    frame.pivot.y < frame.source[1] || frame.pivot.y > frame.source[1] + frame.source[3])
                    throw std::runtime_error("Sprite pivot outside source frame");
                frame.scale = real(item.at("scale"), .01f, 2);
            }
        }
        ++direction;
    }
    const auto& warps = j.at("warp"); fields(warps, {"south", "east", "north"});
    for (auto [key, warp] : {std::pair{"south", &result.south}, {"east", &result.east}, {"north", &result.north}}) {
        const auto& value = warps.at(key); fields(value, {"width", "shift"});
        for (auto [field, values] : {std::pair{"width", &warp->width}, {"shift", &warp->shift}}) {
            const auto& array = value.at(field);
            if (!array.is_array() || array.size() != 6) throw std::runtime_error("Warp requires six horizontal mesh slices");
            for (size_t i = 0; i < 6; ++i) (*values)[i] = real(array[i], -1, 2);
        }
        validate(*warp);
        // The last two slices cover the foot pivot, so the ground remains fixed.
        if (warp->width[4] != 1 || warp->shift[4] != 0) throw std::runtime_error("Warp must pin the feet");
    }
    return result;
}

SpritePixels warpSprite(const SpritePixels& source, const SpriteWarp& warp, float pivotX, bool mirror) {
    validate(source); validate(warp);
    if (!std::isfinite(pivotX) || pivotX < 0 || pivotX > source.width) throw std::invalid_argument("Invalid warp pivot");
    SpritePixels result{source.width, source.height, std::vector<std::uint8_t>(source.bgra.size())};
    const bool identity = warp.width == SpriteWarp{}.width && warp.shift == SpriteWarp{}.shift;
    for (int y = 0; y < source.height; ++y) {
        const float slice = float(y) / std::max(1, source.height - 1) * 5;
        const auto low = std::min(size_t(slice), size_t(4));
        float blend = slice - float(low);
        blend = blend * blend * (3 - 2 * blend); // Smooth joins preserve the ink contours.
        const float width = std::lerp(warp.width[low], warp.width[low + 1], blend);
        const float shift = std::lerp(warp.shift[low], warp.shift[low + 1], blend) * source.width;
        for (int x = 0; x < source.width; ++x) {
            const int outputX = mirror ? source.width - 1 - x : x;
            auto* output = result.bgra.data() + (size_t(y) * source.width + outputX) * 4;
            if (identity) {
                std::copy_n(source.bgra.data() + (size_t(y) * source.width + x) * 4, 4, output);
                continue;
            }
            // Inverse sampling: no holes or overdraw; filter premultiplied colour and alpha together.
            const float sx = (x + .5f - pivotX - shift) / width + pivotX - .5f;
            const int left = int(std::floor(sx));
            const float weight = sx - left;
            for (int channel = 0; channel < 4; ++channel) {
                const auto read = [&](int ix) { return ix < 0 || ix >= source.width ? 0 : source.bgra[(size_t(y) * source.width + ix) * 4 + channel]; };
                output[channel] = static_cast<std::uint8_t>(std::lround(std::lerp(float(read(left)), float(read(left + 1)), weight)));
            }
        }
    }
    return result;
}

SpritePixels assembleDirectionalSprite(const DirectionalSpriteRecipe& recipe,
    const std::array<std::array<SpritePixels, 9>, 2>& frames) {
    if (recipe.width < 1 || recipe.height < 1 || recipe.width > 512 || recipe.height > 512)
        throw std::invalid_argument("Invalid directional canvas size");
    SpritePixels atlas{recipe.width * 9, recipe.height * 8, {}};
    atlas.bgra.resize(size_t(atlas.width) * atlas.height * 4);
    const SpriteWarp identity;
    struct View { size_t source; bool mirror; const SpriteWarp* warp; };
    const std::array<View, 8> views{{
        {0, false, &recipe.south}, {0, false, &identity}, {0, false, &recipe.east}, {1, true, &identity},
        {1, false, &recipe.north}, {1, false, &identity}, {0, true, &recipe.east}, {0, true, &identity}}};
    for (size_t row = 0; row < views.size(); ++row) for (size_t column = 0; column < 9; ++column) {
        const auto& view = views[row]; const auto& original = frames[view.source][column];
        if (original.width != recipe.width || original.height != recipe.height) throw std::invalid_argument("Directional frame canvas mismatch");
        const auto image = warpSprite(original, *view.warp, recipe.pivot.x, view.mirror);
        for (int y = 0; y < recipe.height; ++y)
            std::copy_n(image.bgra.data() + size_t(y) * image.width * 4, size_t(image.width) * 4,
                atlas.bgra.data() + ((row * recipe.height + y) * atlas.width + column * recipe.width) * 4);
    }
    return atlas;
}
}
