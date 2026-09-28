#include "ForgeApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <commdlg.h>

namespace rts::forge {
void ForgeApplication::editorError(const std::exception& error) {
    const std::string value=error.what();
    const int n=MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),nullptr,0);
    std::wstring text(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),text.data(),n);
    if(editor_) editor_->message=L"Не удалось выполнить действие: "+text;
}

std::filesystem::path ForgeApplication::chooseMap(bool save) {
    const auto folder=paths_.writable(L"maps/Моя карта.rtsmap").parent_path();
    std::array<wchar_t,32768> name{};
    const auto initial=save&&editor_&&!editor_->file.empty()?editor_->file.wstring():save?L"Моя карта.rtsmap":L"";
    std::copy_n(initial.c_str(),std::min(initial.size(),name.size()-1),name.data());
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner=window_; dialog.lpstrFile=name.data(); dialog.nMaxFile=static_cast<DWORD>(name.size());
    dialog.lpstrFilter=L"Карты Visages of War (*.rtsmap)\0*.rtsmap\0\0";
    dialog.lpstrDefExt=L"rtsmap"; dialog.lpstrInitialDir=folder.c_str();
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog)) return name.data();
    if(CommDlgExtendedError()) throw std::runtime_error("Map file dialog failed");
    return {};
}

bool ForgeApplication::saveEditor(bool saveAs) {
    try {
        editor_->endStroke();
        auto file=editor_->file;
        if(file.empty()||saveAs) file=chooseMap(true);
        if(file.empty()) return false;
        editor_->validateForPlay(); rts::saveScenario(editor_->scenario(),file);
        editor_->file=std::move(file); editor_->dirty=false;
        editor_->message=L"Сохранено: "+editor_->file.wstring(); return true;
    } catch(const std::exception& e) { editorError(e); return false; }
}

bool ForgeApplication::confirmEditorChanges() {
    if(!editor_) return true;
    editor_->endStroke(); if(!editor_->dirty) return true;
    const int answer=MessageBoxW(window_,L"Сохранить изменения карты?",L"Visages Forge",MB_YESNOCANCEL|MB_ICONQUESTION);
    return answer==IDNO || (answer==IDYES && saveEditor());
}

void ForgeApplication::editorAction(size_t action) {
    try {
        editor_->endStroke();
        if(action==0) { SendMessageW(window_,WM_CLOSE,0,0); return; }
        if(action==1 && confirmEditorChanges()) {
            rts::Scenario blank{rts::Map(64,64),{10,10},{13,12},{}};
            blank.heroSpawn=rts::Cell{14,12}; blank.startingCrystals=300;
            editor_->replace(std::move(blank)); editor_->file.clear(); editor_->dirty=true; resetCamera();
        }
        if(action==2 && confirmEditorChanges()) {
            const auto file=chooseMap(false);
            if(!file.empty()) {
                auto loaded=rts::loadScenario(file,worldAssets_);
                rts::WorldEditor check(loaded,worldAssets_,definitions_); check.validateForPlay();
                editor_->replace(std::move(loaded)); editor_->file=file; resetCamera();
            }
        }
        if(action==3) saveEditor();
        if(action==4) saveEditor(true);
        if(action==5) testEditorMap();
        if(action==6) editor_->undo();
        if(action==7) editor_->redo();
    } catch(const std::exception& e) { editorError(e); }
}
}
