#include "rts/Paths.hpp"
#include <windows.h>
#include <shlobj.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>

namespace rts {
namespace fs = std::filesystem;
Paths::Paths(fs::path assets, fs::path userData) {
    if (!assets.is_absolute() || !userData.is_absolute()) throw std::invalid_argument("Path roots must be absolute");
    assets_ = fs::weakly_canonical(assets);
    userData_ = fs::weakly_canonical(userData);
}
fs::path Paths::executable() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length) throw std::runtime_error("GetModuleFileNameW failed");
        if (length < buffer.size()) return fs::path(std::wstring(buffer.data(), length));
        if (buffer.size() >= 32768) throw std::runtime_error("Executable path too long");
        buffer.resize(buffer.size() * 2);
    }
}
Paths Paths::discover() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &raw)))
        throw std::runtime_error("Cannot locate LocalAppData");
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> folder(raw, &CoTaskMemFree);
    return {executable().parent_path() / L"assets", fs::path(folder.get()) / L"RTS"};
}
fs::path Paths::resolve(const fs::path& root, const fs::path& relative) {
    if (relative.empty() || relative.has_root_path()) throw std::invalid_argument("Expected a relative resource path");
    for (const auto& part : relative) {
        const auto text = part.native();
        if (part == L".." || text.find_first_of(L":*?\"<>|") != std::wstring::npos ||
            (!text.empty() && (text.back() == L' ' || (text.back() == L'.' && text != L"."))))
            throw std::invalid_argument("Invalid resource path component");
        if (std::any_of(text.begin(), text.end(), [](wchar_t ch) { return ch < 32; }))
            throw std::invalid_argument("Control character in resource path");
        auto stem = text.substr(0, text.find(L'.'));
        std::transform(stem.begin(), stem.end(), stem.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
        if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" || stem == L"CONIN$" || stem == L"CONOUT$" ||
            (stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) &&
             ((stem[3] >= L'0' && stem[3] <= L'9') || stem[3] == L'\u00b9' || stem[3] == L'\u00b2' || stem[3] == L'\u00b3')))
            throw std::invalid_argument("Reserved Windows device name in resource path");
    }
    const auto resolved = fs::weakly_canonical(root / relative);
    // Compare components, not string prefixes (assets-other is not inside assets).
    auto candidate = resolved.begin();
    for (auto base = root.begin(); base != root.end(); ++base, ++candidate) {
        if (candidate == resolved.end() || CompareStringOrdinal(base->c_str(), -1, candidate->c_str(), -1, TRUE) != CSTR_EQUAL)
            throw std::invalid_argument("Resource path escapes its root");
    }
    return resolved;
}
fs::path Paths::asset(const fs::path& relative) const {
    const auto path = resolve(assets_, relative);
    if (!fs::is_regular_file(path)) {
        const auto utf8 = path.u8string();
        throw std::runtime_error("Required asset is missing: " + std::string(utf8.begin(), utf8.end()));
    }
    return path;
}
fs::path Paths::optionalAsset(const fs::path& relative) const {
    const auto path = resolve(assets_, relative);
    const auto status = fs::status(path);
    if (status.type() == fs::file_type::not_found) return {};
    if (!fs::is_regular_file(status)) {
        const auto utf8 = path.u8string();
        throw std::runtime_error("Expected an asset file: " + std::string(utf8.begin(), utf8.end()));
    }
    return path;
}
fs::path Paths::writable(const fs::path& relative) const {
    const auto path = resolve(userData_, relative);
    fs::create_directories(path.parent_path());
    return path;
}
}
