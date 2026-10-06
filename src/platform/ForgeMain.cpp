#include "forge/ForgeApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <shellapi.h>
#include <shobjidl.h>
#include <fstream>
#include <iostream>
#include <string_view>

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    bool verify=false,smoke=false,overview=false;std::filesystem::path snapshot,mapFile;
    int count{};auto* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args) return 1;
    for(int i=1;i<count;++i) {
        const std::wstring_view arg=args[i];
        if(arg==L"--verify-assets") verify=true;
        else if(arg==L"--smoke-test") smoke=true;
        else if(arg==L"--snapshot"&&i+1<count) snapshot=args[++i];
        else if(arg==L"--snapshot-overview"&&i+1<count) { snapshot=args[++i]; overview=true; }
        else if(arg==L"--map"&&i+1<count) mapFile=args[++i];
        else {LocalFree(args);return 2;}
    }
    LocalFree(args);
    try {
        rts::platform::ComApartment apartment;SetCurrentProcessExplicitAppUserModelID(L"Visages.Forge");
        rts::forge::ForgeApplication app(rts::Paths::discover(),mapFile);
        if(verify) {app.verify();return 0;}
        if(!snapshot.empty()) {app.snapshot(snapshot,overview);return 0;}
        return app.run(instance,smoke);
    } catch(const std::exception& e) {
        std::cerr<<"Visages Forge: "<<e.what()<<'\n';OutputDebugStringA(e.what());
        if(!verify&&!smoke&&snapshot.empty()) {
            const int n=MultiByteToWideChar(CP_UTF8,0,e.what(),-1,nullptr,0);std::wstring message(static_cast<size_t>(std::max(1,n)),L'\0');
            MultiByteToWideChar(CP_UTF8,0,e.what(),-1,message.data(),n);
            MessageBoxW(nullptr,(L"Не удалось запустить Visages Forge.\n\n"+message).c_str(),L"Visages Forge",MB_OK|MB_ICONERROR);
        }
        return 1;
    }
}
