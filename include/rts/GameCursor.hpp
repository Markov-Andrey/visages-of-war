#pragma once
#include "rts/Paths.hpp"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <map>

namespace rts {
enum class CursorKind { Default, Select, Move, Attack, Gather, Blocked, Rally, Build, Hand, Count };
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
        Microsoft::WRL::ComPtr<IWICBitmap> image;
    };
    std::array<Frame, static_cast<size_t>(CursorKind::Count)> frames_{};
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory_;
    Microsoft::WRL::ComPtr<IWICBitmap> atlas_;
    std::map<std::pair<UINT, CursorKind>, HCURSOR> cursors_;
};
}
