#include "rts/GameCursor.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace rts {
namespace {
void check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("Cannot decode game cursor atlas");
}
struct Bitmap {
    HBITMAP value{};
    ~Bitmap() { if (value) DeleteObject(value); }
};
constexpr std::array<std::string_view, static_cast<size_t>(CursorKind::Count)> names{
    "default", "select", "move", "attack", "gather", "blocked", "rally", "build", "hand", "target",
    "scroll-east", "scroll-south-east", "scroll-south", "scroll-south-west",
    "scroll-west", "scroll-north-west", "scroll-north", "scroll-north-east"
};
constexpr size_t scrollIndex = static_cast<size_t>(CursorKind::ScrollEast);
constexpr size_t sourceCount = scrollIndex + 1;
struct CursorGeometry {
    int sourceWidth{}, sourceHeight{}, width{}, height{}, hotX{}, hotY{};
    float cosine{}, sine{};
};
CursorGeometry geometry(int size, const WICRect& source, int hotX, int hotY, unsigned rotation, UINT dpi) {
    constexpr float diagonal = .70710678118f;
    constexpr std::array<Vec2, 8> turns{{{1, 0}, {diagonal, diagonal}, {0, 1}, {-diagonal, diagonal},
        {-1, 0}, {-diagonal, -diagonal}, {0, -1}, {diagonal, -diagonal}}};
    const auto turn = turns.at(rotation);
    CursorGeometry result;
    result.sourceWidth = std::clamp(MulDiv(size, dpi, 96), 1, 512);
    result.sourceHeight = std::clamp(MulDiv(result.sourceWidth, source.Height, source.Width), 1, 512);
    result.cosine = turn.x; result.sine = turn.y;
    result.width = int(std::ceil(std::abs(turn.x) * result.sourceWidth + std::abs(turn.y) * result.sourceHeight));
    result.height = int(std::ceil(std::abs(turn.y) * result.sourceWidth + std::abs(turn.x) * result.sourceHeight));
    const float hx = std::min(result.sourceWidth - 1, MulDiv(hotX, result.sourceWidth, source.Width)) - (result.sourceWidth - 1) * .5f;
    const float hy = std::min(result.sourceHeight - 1, MulDiv(hotY, result.sourceHeight, source.Height)) - (result.sourceHeight - 1) * .5f;
    result.hotX = std::clamp(int(std::lround(hx * turn.x - hy * turn.y + (result.width - 1) * .5f)), 0, result.width - 1);
    result.hotY = std::clamp(int(std::lround(hx * turn.y + hy * turn.x + (result.height - 1) * .5f)), 0, result.height - 1);
    return result;
}
void rotatePixels(const std::vector<uint32_t>& source, uint32_t* target, const CursorGeometry& shape) {
    for (int y = 0; y < shape.height; ++y) for (int x = 0; x < shape.width; ++x) {
        const float dx = x - (shape.width - 1) * .5f, dy = y - (shape.height - 1) * .5f;
        const float sx = dx * shape.cosine + dy * shape.sine + (shape.sourceWidth - 1) * .5f;
        const float sy = -dx * shape.sine + dy * shape.cosine + (shape.sourceHeight - 1) * .5f;
        const int left = int(std::floor(sx)), top = int(std::floor(sy));
        const float fx = sx - left, fy = sy - top;
        std::array<float, 4> channels{};
        // Bilinear filtering in premultiplied BGRA keeps the translucent outline free of dark fringes.
        for (int iy = 0; iy < 2; ++iy) for (int ix = 0; ix < 2; ++ix) {
            const int px = left + ix, py = top + iy;
            if (px < 0 || py < 0 || px >= shape.sourceWidth || py >= shape.sourceHeight) continue;
            const auto pixel = source[static_cast<size_t>(py) * shape.sourceWidth + px];
            const float weight = (ix ? fx : 1 - fx) * (iy ? fy : 1 - fy);
            for (unsigned channel = 0; channel < channels.size(); ++channel)
                channels[channel] += ((pixel >> (channel * 8)) & 255) * weight;
        }
        uint32_t pixel{};
        for (unsigned channel = 0; channel < channels.size(); ++channel)
            pixel |= uint32_t(std::clamp(std::lround(channels[channel]), 0l, 255l)) << (channel * 8);
        target[static_cast<size_t>(y) * shape.width + x] = pixel;
    }
}
}
GameCursor::GameCursor(const Paths& paths) {
    std::ifstream input(paths.asset(L"ui/cursor.rtsdata"));
    std::string token, image;
    int version{};
    if (!(input >> token >> version) || token != "RTSCURSOR" || version != 2 ||
        !(input >> token >> std::quoted(image)) || token != "IMAGE")
        throw std::runtime_error("Invalid cursor atlas definition");
    const auto path = paths.asset(std::filesystem::path(std::u8string(image.begin(), image.end())));
    using Microsoft::WRL::ComPtr;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory_.GetAddressOf())));
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, frame.GetAddressOf()));
    UINT width{}, height{};
    check(frame->GetSize(&width, &height));
    if (!width || !height || width > 8192 || height > 8192) throw std::runtime_error("Invalid cursor atlas size");
    ComPtr<IWICFormatConverter> converter;
    check(factory_->CreateFormatConverter(converter.GetAddressOf()));
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    check(factory_->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, atlas_.GetAddressOf()));
    std::array<bool, sourceCount> seen{};
    while (input >> token) {
        std::string name;
        Frame entry;
        auto& r = entry.bounds;
        if (token != "CURSOR" || !(input >> name >> r.X >> r.Y >> r.Width >> r.Height >> entry.size >> entry.hotX >> entry.hotY))
            throw std::runtime_error("Invalid cursor frame record");
        const auto sourceEnd = names.begin() + scrollIndex;
        const auto found = std::find(names.begin(), sourceEnd, name);
        if (found == sourceEnd && name != "scroll") throw std::runtime_error("Unknown cursor state: " + name);
        const size_t index = name == "scroll" ? scrollIndex : static_cast<size_t>(found - names.begin());
        if (seen[index] || r.X < 0 || r.Y < 0 || r.Width <= 0 || r.Height <= 0 ||
            r.Width > int(width) || r.Height > int(height) || r.X > int(width) - r.Width || r.Y > int(height) - r.Height ||
            entry.size < 16 || entry.size > 128 || entry.hotX < 0 || entry.hotY < 0 || entry.hotX >= r.Width || entry.hotY >= r.Height)
            throw std::runtime_error("Invalid cursor rectangle, size or hotspot: " + name);
        seen[index] = true; frames_[index] = entry;
    }
    if (!input.eof() || std::find(seen.begin(), seen.end(), false) != seen.end()) throw std::runtime_error("Incomplete cursor atlas definition");
    for (size_t i = 0; i < sourceCount; ++i) {
        auto& entry = frames_[i];
        const auto& r = entry.bounds;
        ComPtr<IWICBitmapClipper> clipper;
        check(factory_->CreateBitmapClipper(clipper.GetAddressOf()));
        check(clipper->Initialize(atlas_.Get(), &r));
        check(factory_->CreateBitmapFromSource(clipper.Get(), WICBitmapCacheOnLoad, entry.image.GetAddressOf()));
        std::vector<uint32_t> pixels(static_cast<size_t>(r.Width) * r.Height);
        check(entry.image->CopyPixels(nullptr, r.Width * 4, static_cast<UINT>(pixels.size() * 4), reinterpret_cast<BYTE*>(pixels.data())));
        // Irregular atlas frames can contain tiny disconnected tips from neighbouring drawings.
        // Select the single connected cursor silhouette before scaling, preserving its original RGBA.
        std::vector<bool> visited(pixels.size());
        std::vector<size_t> component, largest;
        for (size_t start = 0; start < pixels.size(); ++start) {
            if (visited[start] || !(pixels[start] >> 24)) continue;
            component.clear(); component.push_back(start); visited[start] = true;
            for (size_t next = 0; next < component.size(); ++next) {
                const int x = static_cast<int>(component[next] % r.Width), y = static_cast<int>(component[next] / r.Width);
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= r.Width || ny >= r.Height) continue;
                    const size_t p = static_cast<size_t>(ny) * r.Width + nx;
                    if (!visited[p] && (pixels[p] >> 24)) { visited[p] = true; component.push_back(p); }
                }
            }
            if (component.size() > largest.size()) largest = component;
        }
        if (largest.empty()) throw std::runtime_error("Empty cursor frame");
        std::fill(visited.begin(), visited.end(), false);
        for (size_t p : largest) visited[p] = true;
        ComPtr<IWICBitmapLock> lock;
        check(entry.image->Lock(nullptr, WICBitmapLockWrite, lock.GetAddressOf()));
        UINT stride{}, bytes{}; BYTE* data{};
        check(lock->GetStride(&stride)); check(lock->GetDataPointer(&bytes, &data));
        for (int y = 0; y < r.Height; ++y) for (int x = 0; x < r.Width; ++x) {
            const size_t p = static_cast<size_t>(y) * r.Width + x;
            reinterpret_cast<uint32_t*>(data + y * stride)[x] = visited[p] ? pixels[p] : 0;
        }
    }
    for (unsigned rotation = 1; rotation < 8; ++rotation) {
        frames_[scrollIndex + rotation] = frames_[scrollIndex];
        frames_[scrollIndex + rotation].rotation = rotation;
    }
    atlas_.Reset();
}
GameCursor::~GameCursor() {
    for (const auto& [key, cursor] : cursors_) {
        if (GetCursor() == cursor) SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        DestroyCursor(cursor);
    }
}
HCURSOR GameCursor::handle(UINT dpi, CursorKind kind) {
    dpi = std::clamp(dpi ? dpi : 96u, 48u, 768u);
    const auto key = std::make_pair(dpi, kind);
    if (const auto found = cursors_.find(key); found != cursors_.end()) return found->second;
    const auto& entry = frames_.at(static_cast<size_t>(kind));
    const auto& r = entry.bounds;
    const auto shape = geometry(entry.size, r, entry.hotX, entry.hotY, entry.rotation, dpi);
    const int width = shape.width, height = shape.height;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICBitmapScaler> scaler;
    check(factory_->CreateBitmapScaler(scaler.GetAddressOf()));
    // Area filtering keeps the large painted source readable at native cursor sizes.
    check(scaler->Initialize(entry.image.Get(), shape.sourceWidth, shape.sourceHeight, WICBitmapInterpolationModeFant));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* raw{};
    Bitmap color{CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &raw, nullptr, 0)};
    if (!color.value) throw std::runtime_error("Cannot allocate cursor bitmap");
    if (entry.rotation == 0) check(scaler->CopyPixels(nullptr, width * 4, width * height * 4, static_cast<BYTE*>(raw)));
    else {
        std::vector<uint32_t> pixels(static_cast<size_t>(shape.sourceWidth) * shape.sourceHeight);
        check(scaler->CopyPixels(nullptr, shape.sourceWidth * 4, static_cast<UINT>(pixels.size() * 4), reinterpret_cast<BYTE*>(pixels.data())));
        rotatePixels(pixels, static_cast<uint32_t*>(raw), shape);
    }
    const std::vector<BYTE> maskPixels(static_cast<size_t>((width + 15) / 16) * 2 * height, 0);
    Bitmap mask{CreateBitmap(width, height, 1, 1, maskPixels.data())};
    if (!mask.value) throw std::runtime_error("Cannot allocate cursor mask");
    ICONINFO cursorInfo{};
    cursorInfo.fIcon = FALSE;
    cursorInfo.xHotspot = shape.hotX;
    cursorInfo.yHotspot = shape.hotY;
    cursorInfo.hbmColor = color.value; cursorInfo.hbmMask = mask.value;
    const auto cursor = static_cast<HCURSOR>(CreateIconIndirect(&cursorInfo));
    if (!cursor) throw std::runtime_error("Cannot create game cursor");
    try { cursors_.emplace(key, cursor); }
    catch (...) { DestroyCursor(cursor); throw; }
    return cursor;
}
void GameCursor::verify() {
    for (size_t i = 0; i < frames_.size(); ++i) for (UINT dpi : {96u, 144u, 192u}) {
        const auto kind = static_cast<CursorKind>(i);
        const auto& entry = frames_[i];
        ICONINFO info{};
        const auto cursor = handle(dpi, kind);
        if (!GetIconInfo(cursor, &info) || cursor != handle(dpi, kind)) throw std::runtime_error("Cannot inspect cached game cursor");
        Bitmap color{info.hbmColor}, mask{info.hbmMask};
        BITMAP bitmap{};
        const auto shape = geometry(entry.size, entry.bounds, entry.hotX, entry.hotY, entry.rotation, dpi);
        if (!GetObjectW(color.value, sizeof(bitmap), &bitmap) || info.fIcon || bitmap.bmWidth != shape.width || bitmap.bmHeight != shape.height ||
            info.xHotspot != UINT(shape.hotX) || info.yHotspot != UINT(shape.hotY))
            throw std::runtime_error("Cursor DPI size or hotspot is incorrect");
    }
}
void GameCursor::snapshot(const std::filesystem::path& output) {
    constexpr int columns = 3, cellWidth = 660, cellHeight = 440;
    constexpr int width = columns * cellWidth, height = ((int(names.size()) + columns - 1) / columns) * cellHeight;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* raw{};
    Bitmap bitmap{CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &raw, nullptr, 0)};
    if (!bitmap.value) throw std::runtime_error("Cannot allocate cursor preview");
    struct Context {
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ previous{};
        ~Context() { if (previous) SelectObject(dc, previous); if (dc) DeleteDC(dc); }
    } context;
    if (!context.dc) throw std::runtime_error("Cannot create cursor preview context");
    context.previous = SelectObject(context.dc, bitmap.value);
    auto* pixels = static_cast<uint32_t*>(raw);
    std::fill_n(pixels, width * height, 0xff26333fu);
    SetBkMode(context.dc, TRANSPARENT);
    SetTextColor(context.dc, RGB(230, 220, 200));
    for (size_t i = 0; i < names.size(); ++i) {
        const int x = int(i % columns) * cellWidth, y = int(i / columns) * cellHeight;
        TextOutA(context.dc, x + 12, y + 8, names[i].data(), static_cast<int>(names[i].size()));
        if (!DrawIconEx(context.dc, x + 18, y + 32, handle(96, static_cast<CursorKind>(i)), 0, 0, 0, nullptr, DI_NORMAL) ||
            !DrawIconEx(context.dc, x + 235, y + 25, handle(192, static_cast<CursorKind>(i)), 0, 0, 0, nullptr, DI_NORMAL))
            throw std::runtime_error("Cannot draw native cursor preview");
    }
    GdiFlush();
    for (int i = 0; i < width * height; ++i) pixels[i] |= 0xff000000u;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICStream> stream;
    check(factory_->CreateStream(stream.GetAddressOf()));
    check(stream->InitializeFromFilename(output.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory_->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()));
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr));
    check(frame->Initialize(nullptr));
    check(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format));
    check(frame->WritePixels(height, width * 4, width * height * 4, static_cast<BYTE*>(raw)));
    check(frame->Commit()); check(encoder->Commit());
}
}
