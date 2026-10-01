#pragma once
#include "rts/Paths.hpp"
#include "rts/Types.hpp"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <map>

namespace rts {
enum class CursorKind {
    Default, Select, Move, Attack, Gather, Blocked, Rally, Build, Hand, Target,
    ScrollEast, ScrollSouthEast, ScrollSouth, ScrollSouthWest,
    ScrollWest, ScrollNorthWest, ScrollNorth, ScrollNorthEast, Count
};
inline CursorKind scrollCursor(Vec2 direction) {
    if (direction.y < 0) return direction.x < 0 ? CursorKind::ScrollNorthWest : direction.x > 0 ? CursorKind::ScrollNorthEast : CursorKind::ScrollNorth;
    if (direction.y > 0) return direction.x < 0 ? CursorKind::ScrollSouthWest : direction.x > 0 ? CursorKind::ScrollSouthEast : CursorKind::ScrollSouth;
    return direction.x < 0 ? CursorKind::ScrollWest : direction.x > 0 ? CursorKind::ScrollEast : CursorKind::Default;
}
class GameCursor {
public:
    explicit GameCursor(const Paths& paths);
    ~GameCursor();
    GameCursor(const GameCursor&) = delete;
    GameCursor& operator=(const GameCursor&) = delete;
    HCURSOR handle(UINT dpi, CursorKind kind = CursorKind::Default);
    void verify();
    void snapshot(const std::filesystem::path& output);
private:
    struct Frame {
        WICRect bounds{}; int size{}, hotX{}, hotY{};
        unsigned rotation{}; // Clockwise eighth-turns of the single right-facing scroll arrow.
        Microsoft::WRL::ComPtr<IWICBitmap> image;
    };
    std::array<Frame, static_cast<size_t>(CursorKind::Count)> frames_{};
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory_;
    Microsoft::WRL::ComPtr<IWICBitmap> atlas_;
    std::map<std::pair<UINT, CursorKind>, HCURSOR> cursors_;
};
}
