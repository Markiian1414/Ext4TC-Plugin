#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <string>
#include "config_dialog.h"
#include "../wfx/plugin_state.h"
#include "../wfx/lang.h"

#pragma comment(lib, "comdlg32.lib")

// MessageBoxA не підтримує UTF-8. Ця обгортка викликає MessageBoxW.
static int MsgBoxU(HWND hwnd, const char* textUtf8, const char* titleUtf8, UINT uType) {
    auto toW = [](const char* s) -> std::wstring {
        if (!s || !*s) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
        return w;
        };
    return MessageBoxW(hwnd, toW(textUtf8).c_str(), toW(titleUtf8).c_str(), uType);
}

// -------------------------------------------------------
//  Layout constants (dialog units)
// -------------------------------------------------------
//  Вікно: 320 x 78 (Mount) / 320 x 58 (Config)
//  Відступи: PAD=12 зліва/справа, перший рядок y=14
//  Крок між рядками: 20 du
//  Кнопки: ширина 50, висота 14, відступ між ними 6
// -------------------------------------------------------

#define DLG_W        320
#define DLG_PAD       12
#define DLG_BTN_W     50
#define DLG_BTN_H     14
#define DLG_BTN_GAP    6
#define DLG_ROW_H     20
#define DLG_FIRST_ROW 14

// -------------------------------------------------------
//  Resource IDs
// -------------------------------------------------------
#define IDC_EDIT_PATH    101
#define IDC_BTN_BROWSE   102
#define IDC_CHK_READONLY 104
#define IDC_STATIC_PATH  106
#define IDC_RADIO_EN     107   // Radio: English
#define IDC_RADIO_UK     108   // Radio: Українська
#define IDC_STATIC_LANG  109   // Label "Language:"

struct MountDlgData {
    PluginConfig* cfg;
    bool          isConfig; // true = ShowConfigDialog (без Path/Browse)
    std::string   language; // поточна мова ("EN"/"UK"), для config-діалогу
};

static INT_PTR CALLBACK MountDlgProc(HWND hDlg, UINT msg,
    WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_INITDIALOG: {
        auto* data = (MountDlgData*)lParam;
        SetWindowLongPtrA(hDlg, GWLP_USERDATA, (LONG_PTR)data);

        if (!data->isConfig)
            SetDlgItemTextA(hDlg, IDC_EDIT_PATH, data->cfg->mountPath.c_str());

        CheckDlgButton(hDlg, IDC_CHK_READONLY,
            data->cfg->readOnly ? BST_CHECKED : BST_UNCHECKED);

        if (data->isConfig) {
            ShowWindow(GetDlgItem(hDlg, IDC_STATIC_PATH), SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_EDIT_PATH), SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_BTN_BROWSE), SW_HIDE);

            // Встановлюємо відмітку поточної мови
            bool isUK = (data->language == "UK");
            CheckDlgButton(hDlg, IDC_RADIO_EN, isUK ? BST_UNCHECKED : BST_CHECKED);
            CheckDlgButton(hDlg, IDC_RADIO_UK, isUK ? BST_CHECKED : BST_UNCHECKED);
        }
        return TRUE;
    }

    case WM_COMMAND: {
        auto* data = (MountDlgData*)GetWindowLongPtrA(hDlg, GWLP_USERDATA);
        int id = LOWORD(wParam);

        if (id == IDC_BTN_BROWSE) {
            OPENFILENAMEA ofn{};
            char fileBuf[MAX_PATH]{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hDlg;
            // Фільтр: нема нульових байт у рядку C — треба передати буфер вручну
            ofn.lpstrFilter =
                "Disk images (*.img;*.bin;*.raw)\0*.img;*.bin;*.raw\0"
                "All files (*.*)\0*.*\0";
            ofn.lpstrFile = fileBuf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            ofn.lpstrTitle = L10n::S("dlg_browse_title");
            if (GetOpenFileNameA(&ofn))
                SetDlgItemTextA(hDlg, IDC_EDIT_PATH, fileBuf);
        }
        else if (id == IDOK) {
            if (!data->isConfig) {
                char buf[MAX_PATH]{};
                GetDlgItemTextA(hDlg, IDC_EDIT_PATH, buf, MAX_PATH);

                // 1. Порожній рядок
                if (buf[0] == '\0') {
                    MsgBoxU(hDlg,
                        L10n::S("val_empty_text"),
                        L10n::S("val_empty_title"),
                        MB_ICONWARNING | MB_OK);
                    SetFocus(GetDlgItem(hDlg, IDC_EDIT_PATH));
                    break;
                }

                bool isPhysical = (buf[0] == '\\' && buf[1] == '\\' &&
                    buf[2] == '.' && buf[3] == '\\');

                if (!isPhysical) {
                    DWORD attr = GetFileAttributesA(buf);
                    if (attr == INVALID_FILE_ATTRIBUTES) {
                        std::string msg = L10n::Fmt(L10n::S("val_notfound_text"), "{PATH}", buf);
                        MsgBoxU(hDlg, msg.c_str(),
                            L10n::S("val_notfound_title"), MB_ICONWARNING | MB_OK);
                        SetFocus(GetDlgItem(hDlg, IDC_EDIT_PATH));
                        break;
                    }
                    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                        MsgBoxU(hDlg,
                            L10n::S("val_isdir_text"),
                            L10n::S("val_isdir_title"),
                            MB_ICONWARNING | MB_OK);
                        SetFocus(GetDlgItem(hDlg, IDC_EDIT_PATH));
                        break;
                    }
                }

                data->cfg->mountPath = buf;
            }
            else {
                // Config-режим: зберігаємо вибрану мову
                bool ukSelected = (IsDlgButtonChecked(hDlg, IDC_RADIO_UK) == BST_CHECKED);
                data->language = ukSelected ? "UK" : "EN";
            }

            data->cfg->readOnly =
                (IsDlgButtonChecked(hDlg, IDC_CHK_READONLY) == BST_CHECKED);
            EndDialog(hDlg, IDOK);
        }
        else if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        break;
    }
    }
    return FALSE;
}

// -------------------------------------------------------
//  Динамічна побудова шаблону діалогу
//
//  Mount  (isMount=true):
//    Path:  [________path edit________] [Browse]
//    [x] Read-only access
//                              [Mount]  [Cancel]
//
//  Config (isMount=false):
//    [x] Read-only access (default for new mounts)
//                                 [OK]  [Cancel]
// -------------------------------------------------------
static HGLOBAL BuildDialogTemplate(bool isMount)
{
    // Mount: 6 контролів; Config: 6 (checkbox + label + 2 radio + OK + Cancel)
    const short itemCount = 6;
    const short dlgH = 78;

    HGLOBAL hMem = GlobalAlloc(GMEM_ZEROINIT, 8192);
    if (!hMem) return nullptr;
    WORD* p = (WORD*)GlobalLock(hMem);
    if (!p) { GlobalFree(hMem); return nullptr; }

    // --- DLGTEMPLATE ---
    auto* dt = (DLGTEMPLATE*)p;
    dt->style = WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU
        | DS_MODALFRAME | DS_CENTER | DS_SETFONT;
    dt->dwExtendedStyle = 0;
    dt->cdit = (WORD)itemCount;
    dt->x = 0; dt->y = 0;
    dt->cx = DLG_W; dt->cy = dlgH;
    p = (WORD*)(dt + 1);

    // menu (empty), class (default), title
    *p++ = 0;
    *p++ = 0;

    // Заголовок діалогу через локалізацію (конвертуємо з ASCII у UTF-16)
    const char* titleA = isMount ? L10n::S("dlg_mount_title")
        : L10n::S("dlg_config_title");
    wchar_t titleW[128]{};
    MultiByteToWideChar(CP_UTF8, 0, titleA, -1, titleW, 128);
    wcscpy((wchar_t*)p, titleW);
    p += wcslen(titleW) + 1;

    // 9pt Segoe UI
    *p++ = 9;
    wcscpy((wchar_t*)p, L"Segoe UI");
    p += wcslen(L"Segoe UI") + 1;
    if ((uintptr_t)p & 2) p++;

    // Допоміжна лямбда для конвертації рядків локалізації у wchar_t
    auto toW = [](const char* src, wchar_t* dst, int dstLen) {
        MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dstLen);
        };

    auto addItem = [&](short x, short y, short cx, short cy,
        WORD id, DWORD style, DWORD exStyle,
        const wchar_t* cls, const wchar_t* text)
        {
            if ((uintptr_t)p & 2) p++;
            auto* it = (DLGITEMTEMPLATE*)p;
            it->style = WS_CHILD | WS_VISIBLE | style;
            it->dwExtendedStyle = exStyle;
            it->x = x; it->y = y; it->cx = cx; it->cy = cy;
            it->id = id;
            p = (WORD*)(it + 1);
            wcscpy((wchar_t*)p, cls);  p += wcslen(cls) + 1;
            wcscpy((wchar_t*)p, text); p += wcslen(text) + 1;
            *p++ = 0;
        };

    // Варіант addItem з char* (локалізований рядок)
    auto addItemA = [&](short x, short y, short cx, short cy,
        WORD id, DWORD style, DWORD exStyle,
        const wchar_t* cls, const char* textA)
        {
            wchar_t textW[256]{};
            MultiByteToWideChar(CP_UTF8, 0, textA, -1, textW, 256);
            addItem(x, y, cx, cy, id, style, exStyle, cls, textW);
        };

    // --- Розрахунок координат ---
    const short PAD = DLG_PAD;
    const short INNER = DLG_W - 2 * PAD;
    const short BROWSE_W = 54;
    const short BROWSE_X = DLG_W - PAD - BROWSE_W;
    const short LABEL_W = 32;
    const short EDIT_X = PAD + LABEL_W + 4;
    const short EDIT_W = BROWSE_X - EDIT_X - 4;
    const short BTN2_X = DLG_W - PAD - DLG_BTN_W;
    const short BTN1_X = BTN2_X - DLG_BTN_GAP - DLG_BTN_W;

    if (isMount) {
        // Рядок 1 — Path (y=14)
        const short R1 = DLG_FIRST_ROW;
        addItemA(PAD, R1 + 2, LABEL_W, 8,
            IDC_STATIC_PATH, SS_LEFT | SS_CENTERIMAGE, 0,
            L"STATIC", L10n::S("dlg_path_label"));
        addItem(EDIT_X, R1, EDIT_W, 12,
            IDC_EDIT_PATH,
            WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
            L"EDIT", L"");
        addItemA(BROWSE_X, R1, BROWSE_W, 12,
            IDC_BTN_BROWSE, BS_PUSHBUTTON | WS_TABSTOP, 0,
            L"BUTTON", L10n::S("dlg_browse_btn"));

        // Рядок 2 — Read-only (y=34)
        const short R2 = R1 + DLG_ROW_H;
        addItemA(PAD, R2, INNER, 10,
            IDC_CHK_READONLY, BS_AUTOCHECKBOX | WS_TABSTOP, 0,
            L"BUTTON", L10n::S("dlg_readonly_chk"));

        // Рядок 3 — кнопки (y=50)
        const short R3 = R2 + DLG_ROW_H - 4;
        addItemA(BTN1_X, R3, DLG_BTN_W, DLG_BTN_H,
            IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP, 0,
            L"BUTTON", L10n::S("dlg_mount_btn"));
        addItemA(BTN2_X, R3, DLG_BTN_W, DLG_BTN_H,
            IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP, 0,
            L"BUTTON", L10n::S("dlg_cancel_btn"));
    }
    else {
        // Config-режим: checkbox + мітка мови + два radio + OK + Cancel

        // Рядок 1 — Read-only (y=14)
        const short R1 = DLG_FIRST_ROW;
        addItemA(PAD, R1, INNER, 10,
            IDC_CHK_READONLY, BS_AUTOCHECKBOX | WS_TABSTOP, 0,
            L"BUTTON", L10n::S("dlg_readonly_cfg"));

        // Рядок 2 — мітка "Language:" + radio EN + radio UK (y=30)
        const short R2 = R1 + DLG_ROW_H - 4;
        const short LANG_LBL_W = 52;
        const short RADIO_W = 40;
        addItemA(PAD, R2 + 1, LANG_LBL_W, 9,
            IDC_STATIC_LANG, SS_LEFT, 0,
            L"STATIC", L10n::S("dlg_language_lbl"));
        addItem(PAD + LANG_LBL_W + 2, R2, RADIO_W, 10,
            IDC_RADIO_EN,
            BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP, 0,
            L"BUTTON", L"English");
        addItem(PAD + LANG_LBL_W + 2 + RADIO_W + 4, R2, 60, 10,
            IDC_RADIO_UK,
            BS_AUTORADIOBUTTON | WS_TABSTOP, 0,
            L"BUTTON", L"\u0423\u043a\u0440\u0430\u0457\u043d\u0441\u044c\u043a\u0430");
        //           ↑ "Українська" в Unicode escape щоб уникнути проблем з кодуванням вихідника

        // Рядок 3 — кнопки (y=50)
        const short R3 = R2 + DLG_ROW_H;
        addItemA(BTN1_X, R3, DLG_BTN_W, DLG_BTN_H,
            IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP, 0,
            L"BUTTON", L10n::S("dlg_ok_btn"));
        addItemA(BTN2_X, R3, DLG_BTN_W, DLG_BTN_H,
            IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP, 0,
            L"BUTTON", L10n::S("dlg_cancel_btn"));
    }

    GlobalUnlock(hMem);
    return hMem;
}

// -------------------------------------------------------
//  Публічні функції
// -------------------------------------------------------

bool ShowConfigDialog(HWND parent, PluginConfig& cfg)
{
    auto& ps = PluginState::Get();
    MountDlgData data{ &cfg, true, ps.defaultLanguage };
    HGLOBAL hTmpl = BuildDialogTemplate(false);
    if (!hTmpl) return false;

    auto* pTmpl = (DLGTEMPLATE*)GlobalLock(hTmpl);
    if (!pTmpl) { GlobalFree(hTmpl); return false; }

    INT_PTR r = DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr), pTmpl,
        parent, MountDlgProc, (LPARAM)&data);

    GlobalUnlock(hTmpl);
    GlobalFree(hTmpl);

    if (r == IDOK) {
        // Зберігаємо вибрану мову у стані плагіна та у файлі ini
        ps.defaultLanguage = data.language;
        ps.SaveConfig();
        return true;
    }
    return false;
}

bool ShowMountDialog(HWND parent, PluginConfig& cfg)
{
    MountDlgData data{ &cfg, false };
    HGLOBAL hTmpl = BuildDialogTemplate(true);
    if (!hTmpl) return false;

    auto* pTmpl = (DLGTEMPLATE*)GlobalLock(hTmpl);
    if (!pTmpl) { GlobalFree(hTmpl); return false; }

    INT_PTR r = DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr), pTmpl,
        parent, MountDlgProc, (LPARAM)&data);

    GlobalUnlock(hTmpl);
    GlobalFree(hTmpl);
    return r == IDOK;
}