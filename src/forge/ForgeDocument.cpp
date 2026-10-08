#include "ForgeApplication.hpp"
#include "platform/WindowsSupport.hpp"
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <commdlg.h>

namespace rts::forge {
namespace {
INT_PTR CALLBACK mapSizeDialog(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        for (int control : {201, 202}) {
            for (int value = 32; value <= 512; value += 32)
                SendDlgItemMessageW(window, control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(std::to_wstring(value).c_str()));
            SendDlgItemMessageW(window, control, CB_SETCURSEL, 1, 0);
        }
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) { EndDialog(window, IDCANCEL); return TRUE; }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) {
        const Cell size{32 * (1 + int(SendDlgItemMessageW(window, 201, CB_GETCURSEL, 0, 0))),
                        32 * (1 + int(SendDlgItemMessageW(window, 202, CB_GETCURSEL, 0, 0)))};
        if (size.x / 2 + size.y > 512) {
            MessageBoxW(window, L"Слишком большая карта. Уменьшите ширину или высоту.", L"Visages Forge", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        *reinterpret_cast<Cell*>(GetWindowLongPtrW(window, DWLP_USER)) = size;
        EndDialog(window, IDOK); return TRUE;
    }
    return FALSE;
}
}
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
            Cell dimensions{64,64};
            const auto result=DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(200), window_, mapSizeDialog,
                reinterpret_cast<LPARAM>(&dimensions));
            if (result == -1) throw std::runtime_error("Cannot open map dimensions dialog");
            if (result != IDOK) return;
            auto map = Map::rectangular(dimensions.x, dimensions.y);
            const Cell middle{map.width()/2,map.height()/2};
            const auto& depot=editor_->startingDepot();
            const Cell hall=middle-Cell{depot.width/2,depot.height/2};
            const Cell worker=hall+Cell{depot.width+1,depot.height/2};
            rts::Scenario blank{std::move(map),hall,worker,{}};
            blank.heroSpawn=worker+Cell{0,2}; blank.startingCrystals=300;
            editor_->replace(std::move(blank)); editor_->file.clear(); editor_->dirty=true; resetCamera();
        }
        if(action==2 && confirmEditorChanges()) {
            const auto file=chooseMap(false);
            if(!file.empty()) {
                const auto& depot=editor_->startingDepot();
                auto loaded=rts::loadScenario(file,worldAssets_,{depot.width,depot.height});
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
