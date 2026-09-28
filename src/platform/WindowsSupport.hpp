#pragma once
#include "rts/Definitions.hpp"
#include "rts/Paths.hpp"
#include <windows.h>
#include <objbase.h>
#include <stdexcept>
#include <string>

namespace rts::platform {
class ComApartment {
public:
    ComApartment() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
            throw std::runtime_error("COM initialization failed");
    }
    ~ComApartment() { CoUninitialize(); }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
};
inline Definitions loadDefinitions(const Paths& paths) {
    return Definitions::load(paths.asset(L"data/catalog.json"));
}
inline std::wstring wide(const std::string& value) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count == 0 && !value.empty()) throw std::runtime_error("Invalid UTF-8 UI text");
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
}
