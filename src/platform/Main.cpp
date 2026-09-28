#include "game/GameApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <shellapi.h>
#include <shobjidl.h>
#include <fstream>
#include <iostream>
#include <string_view>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    bool verify = false, smoke = false, smokeMap = false, forgeTest = false, menuSnapshot = false, gridSnapshot = false, titleSnapshot = false;
    std::filesystem::path snapshot, mapFile;
    int count{};
    auto* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 1;
    for (int i = 1; i < count; ++i) {
        const std::wstring_view arg(args[i]);
        if (arg == L"--verify-assets") verify = true;
        else if (arg == L"--smoke-test") smoke = true;
        else if (arg == L"--smoke-map") { smoke=true; smokeMap=true; }
        else if (arg == L"--forge-test") forgeTest=true;
        else if (arg == L"--map" && i+1<count) mapFile=args[++i];
        else if (arg == L"--snapshot" && i + 1 < count) snapshot = args[++i];
        else if (arg == L"--snapshot-grid" && i + 1 < count) { snapshot = args[++i]; gridSnapshot = true; }
        else if (arg == L"--snapshot-menu" && i + 1 < count) { snapshot = args[++i]; menuSnapshot = true; }
        else if (arg == L"--snapshot-title" && i + 1 < count) { snapshot = args[++i]; titleSnapshot = true; }
        else { LocalFree(args); return 2; }
    }
    LocalFree(args);
    if ((forgeTest || smokeMap) && mapFile.empty()) return 2;
    try {
        rts::platform::ComApartment apartment;
        SetCurrentProcessExplicitAppUserModelID(L"Visages.Game");
        const auto paths = rts::Paths::discover();
        rts::game::GameApplication app(paths,mapFile,forgeTest);
        if (verify) { app.verify(); return 0; }
        if (!snapshot.empty()) { app.snapshot(snapshot, menuSnapshot, gridSnapshot, titleSnapshot); return 0; }
        if (!smoke) {
            std::ofstream log(paths.writable(L"logs/RTS.log"), std::ios::app);
            log << "RTS started\n";
        }
        return app.run(instance, smoke, smokeMap);
    } catch (const std::exception& error) {
        std::cerr << "RTS: " << error.what() << '\n';
        OutputDebugStringA(error.what());
        if (!verify && !smoke && snapshot.empty()) {
            const int countChars = MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, nullptr, 0);
            std::wstring details(static_cast<size_t>(std::max(1, countChars)), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, details.data(), countChars);
            const auto message = L"Не удалось запустить Visages of War. Рядом с файлом игры должна находиться папка assets.\n\n" + details;
            MessageBoxW(nullptr, message.c_str(), L"Visages of War", MB_OK | MB_ICONERROR);
        }
        return 1;
    }
}
