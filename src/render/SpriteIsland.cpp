#include "rts/SpriteIsland.hpp"
#include <algorithm>
#include <stdexcept>

namespace rts {
SpritePixels extractSpriteIsland(const SpritePixels& sheet, std::array<int, 4> source, Cell seed) {
    const auto [left, top, width, height] = source;
    if (sheet.width < 1 || sheet.height < 1 || sheet.width > 8192 || sheet.height > 8192 ||
        sheet.bgra.size() != size_t(sheet.width) * sheet.height * 4 || left < 0 || top < 0 ||
        width < 1 || height < 1 || width > sheet.width || height > sheet.height ||
        left > sheet.width - width || top > sheet.height - height ||
        seed.x < left || seed.y < top || seed.x >= left + width || seed.y >= top + height)
        throw std::invalid_argument("Invalid sprite island source or seed");
    const auto pixel = [&](int x, int y) { return sheet.bgra.data() + (size_t(top + y) * sheet.width + left + x) * 4; };
    const int start = (seed.y - top) * width + seed.x - left;
    if (pixel(start % width, start / width)[3] <= 8) throw std::invalid_argument("Sprite island seed is transparent");
    std::vector<std::uint8_t> mask(size_t(width) * height);
    std::vector<int> queue{start}; mask[start] = 1;
    for (size_t next = 0; next < queue.size(); ++next) {
        const int index = queue[next], x = index % width, y = index / width;
        for (const Cell d : {Cell{-1, 0}, Cell{1, 0}, Cell{0, -1}, Cell{0, 1}}) {
            const int nx = x + d.x, ny = y + d.y;
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            const int neighbor = ny * width + nx;
            if (!mask[neighbor] && pixel(nx, ny)[3] > 8) { mask[neighbor] = 1; queue.push_back(neighbor); }
        }
    }
    // Preserve the original low-alpha antialiasing around the selected component.
    for (int index : queue) {
        const int x = index % width, y = index / width;
        for (int ny = std::max(0, y - 1); ny <= std::min(height - 1, y + 1); ++ny)
            for (int nx = std::max(0, x - 1); nx <= std::min(width - 1, x + 1); ++nx)
                if (pixel(nx, ny)[3] <= 8) mask[ny * width + nx] = 1;
    }
    SpritePixels result{width, height, std::vector<std::uint8_t>(size_t(width) * height * 4)};
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        if (mask[y * width + x]) std::copy_n(pixel(x, y), 4, result.bgra.data() + (size_t(y) * width + x) * 4);
    return result;
}
}
