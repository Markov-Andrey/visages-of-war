#include "ForgeApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace rts::forge {
void ForgeApplication::launchGame() {
    if(gameProcess_) {editor_->message=L"Проверка карты уже запущена.";return;}
    editor_->endStroke(); editor_->validateForPlay();
    const auto directory=rts::Paths::executable().parent_path();
    const auto executable=directory/L"Visages of War.exe";
    if(!std::filesystem::is_regular_file(executable)) throw std::runtime_error("Visages of War.exe must be next to Visages Forge.exe for test play");
    const auto filename=L"preview-"+std::to_wstring(GetCurrentProcessId())+L".rtsmap";
    testFile_=smoke_?directory/(L"Forge проверка "+filename):paths_.writable(std::filesystem::path(L"forge/previews")/filename);
    rts::saveScenario(editor_->scenario(),testFile_);
    std::wstring command=L"\""+executable.wstring()+L"\" --map \""+testFile_.wstring()+L"\" --forge-test";
    if(smoke_) command+=L" --smoke-map";
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if(smoke_) {startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;}
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,directory.c_str(),&startup,&process)) {
        const auto failure=GetLastError();
        std::error_code error; std::filesystem::remove(testFile_,error); testFile_.clear();
        throw std::runtime_error("Cannot launch Visages of War: "+std::to_string(failure));
    }
    CloseHandle(process.hThread); gameProcess_=process.hProcess;
    paintAtTest_=editor_->scenario().landscape.paint.size(); decorAtTest_=editor_->scenario().landscape.decorations.size();
    unitsAtTest_=editor_->scenario().units.size(); dirtyAtTest_=editor_->dirty;
    editor_->message=L"Карта запущена в Visages of War. F10 в игре — вернуться в Forge.";
    if(!smoke_) ShowWindow(window_,SW_MINIMIZE);
}

void ForgeApplication::pollGame() {
    if(!gameProcess_) return;
    const auto state=WaitForSingleObject(gameProcess_,0); if(state==WAIT_TIMEOUT) return;
    if(state!=WAIT_OBJECT_0) throw std::runtime_error("Cannot observe test game process");
    DWORD result{}; if(!GetExitCodeProcess(gameProcess_,&result)) throw std::runtime_error("Cannot read test game exit code");
    CloseHandle(gameProcess_); gameProcess_=nullptr;
    std::error_code error; std::filesystem::remove(testFile_,error); testFile_.clear();
    if(smoke_) {
        if(result!=0) throw std::runtime_error("Forge smoke: game process failed: "+std::to_string(result));
        if(editor_->scenario().landscape.paint.size()!=paintAtTest_||editor_->scenario().landscape.decorations.size()!=decorAtTest_||
            editor_->scenario().units.size()!=unitsAtTest_||editor_->dirty!=dirtyAtTest_)
            throw std::runtime_error("Forge smoke: test play changed the document");
        const auto paint=editor_->scenario().landscape.paint;
        editor_->undo();
        if(editor_->scenario().landscape.paint.size()>=paint.size()) throw std::runtime_error("Forge smoke: test lost undo history");
        editor_->redo();
        if(editor_->scenario().landscape.paint!=paint) throw std::runtime_error("Forge smoke: test lost redo history");
        testFinished_=true;
    } else { ShowWindow(window_,SW_RESTORE); SetForegroundWindow(window_); }
    editor_->message=result==0?L"Проверка завершена. Карта и история изменений сохранены в Forge.":L"Игра завершилась с ошибкой. Редактируемая карта не изменилась.";
}

void ForgeApplication::testEditorMap() { launchGame(); }
}
