#define _CRT_SECURE_NO_WARNINGS // Вимикаємо попередження про wcscpy
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <string>
#include "config_dialog.h"
#include "../wfx/plugin_state.h"

#pragma comment(lib, "comdlg32.lib")

// -------------------------------------------------------
//  Resource IDs
// -------------------------------------------------------
#define IDC_EDIT_PATH       101
#define IDC_BTN_BROWSE      102
#define IDC_EDIT_OFFSET     103
#define IDC_CHK_READONLY    104
#define IDC_COMBO_ENC       105
#define IDC_STATIC_PATH     106
#define IDC_STATIC_OFFSET   107
#define IDC_STATIC_ENC      108
#define IDC_STATIC_INFO     109
#define IDC_BTN_OK          IDOK
#define IDC_BTN_CANCEL      IDCANCEL

struct MountDlgData {
    PluginConfig* cfg;
    bool          isConfig;
};

static INT_PTR CALLBACK MountDlgProc(HWND hDlg, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* data = (MountDlgData*)lParam;
        SetWindowLongPtrA(hDlg, GWLP_USERDATA, (LONG_PTR)data);

        HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_ENC);
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"UTF-8");
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"Latin-1");
        int sel = (data->cfg->encoding == "Latin-1") ? 1 : 0;
        SendMessageA(hCombo, CB_SETCURSEL, sel, 0);

        SetDlgItemTextA(hDlg, IDC_EDIT_PATH, data->cfg->mountPath.c_str());

        char offsetBuf[32]{};
        _ui64toa_s(data->cfg->partOffset, offsetBuf, sizeof(offsetBuf), 10);
        SetDlgItemTextA(hDlg, IDC_EDIT_OFFSET, offsetBuf);

        CheckDlgButton(hDlg, IDC_CHK_READONLY,
            data->cfg->readOnly ? BST_CHECKED : BST_UNCHECKED);

        if (data->isConfig) {
            ShowWindow(GetDlgItem(hDlg, IDC_EDIT_PATH), SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_BTN_BROWSE), SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_EDIT_OFFSET), SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_STATIC_PATH), SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_STATIC_OFFSET), SW_HIDE);
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
            ofn.lpstrFilter = "Disk images (*.img;*.bin;*.raw)\0*.img;*.bin;*.raw\0All files (*.*)\0*.*\0";
            ofn.lpstrFile = fileBuf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            ofn.lpstrTitle = "Select Ext2/3/4 disk image";

            if (GetOpenFileNameA(&ofn))
                SetDlgItemTextA(hDlg, IDC_EDIT_PATH, fileBuf);
        }
        else if (id == IDOK) {
            char buf[MAX_PATH]{};
            GetDlgItemTextA(hDlg, IDC_EDIT_PATH, buf, MAX_PATH);
            data->cfg->mountPath = buf;

            char offBuf[32]{};
            GetDlgItemTextA(hDlg, IDC_EDIT_OFFSET, offBuf, sizeof(offBuf));
            data->cfg->partOffset = _strtoui64(offBuf, nullptr, 10);

            data->cfg->readOnly = (IsDlgButtonChecked(hDlg, IDC_CHK_READONLY) == BST_CHECKED);

            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_ENC);
            int sel = (int)SendMessageA(hCombo, CB_GETCURSEL, 0, 0);
            data->cfg->encoding = (sel == 1) ? "Latin-1" : "UTF-8";

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

static HGLOBAL BuildDialogTemplate(bool isMount) {
    size_t bufSize = 4096;
    HGLOBAL hMem = GlobalAlloc(GMEM_ZEROINIT, bufSize);
    if (!hMem) return nullptr;

    WORD* p = (WORD*)GlobalLock(hMem);
    if (!p) {
        GlobalFree(hMem);
        return nullptr;
    }

    DLGTEMPLATE* dt = (DLGTEMPLATE*)p;
    dt->style = WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU
        | DS_MODALFRAME | DS_CENTER | DS_SETFONT;
    dt->dwExtendedStyle = 0;
    dt->cdit = isMount ? 10 : 6;
    dt->x = 0; dt->y = 0;

    // ВИПРАВЛЕНО РОЗМІРИ ВІКНА:
    dt->cx = 310;
    dt->cy = isMount ? 135 : 80;

    p = (WORD*)(dt + 1);

    *p++ = 0x0000;
    *p++ = 0x0000;
    const wchar_t* title = isMount ? L"Mount Ext2/3/4 Volume" : L"Ext4TC Plugin Settings";
    wcscpy((wchar_t*)p, title);
    p += wcslen(title) + 1;
    *p++ = 9;
    wcscpy((wchar_t*)p, L"Segoe UI");
    p += wcslen(L"Segoe UI") + 1;
    if ((uintptr_t)p & 2) p++;

    auto addItem = [&](short x, short y, short cx, short cy,
        WORD id, DWORD style, DWORD exStyle,
        const wchar_t* cls, const wchar_t* text) {
            if ((uintptr_t)p & 2) p++;
            DLGITEMTEMPLATE* it = (DLGITEMTEMPLATE*)p;
            it->style = style | WS_CHILD | WS_VISIBLE;
            it->dwExtendedStyle = exStyle;
            it->x = x; it->y = y; it->cx = cx; it->cy = cy;
            it->id = id;
            p = (WORD*)(it + 1);
            *p++ = 0xFFFF;
            *p++ = 0x0000;
            p -= 2;
            wcscpy((wchar_t*)p, cls);
            p += wcslen(cls) + 1;
            wcscpy((wchar_t*)p, text);
            p += wcslen(text) + 1;
            *p++ = 0;
            if ((uintptr_t)p & 2) p++;
        };

    int row = 8;
    if (isMount) {
        addItem(4, (short)row, 28, 8, IDC_STATIC_PATH, SS_LEFT, 0, L"STATIC", L"Path:");
        addItem(36, (short)row, 196, 12, IDC_EDIT_PATH,
            WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, L"EDIT", L"");
        addItem(236, (short)row, 60, 12, IDC_BTN_BROWSE,
            BS_PUSHBUTTON | WS_TABSTOP, 0, L"BUTTON", L"Browse...");
        row += 18;
        addItem(4, (short)row, 60, 8, IDC_STATIC_OFFSET, SS_LEFT, 0, L"STATIC", L"Partition offset (bytes):");
        addItem(68, (short)row, 80, 12, IDC_EDIT_OFFSET,
            WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, L"EDIT", L"0");
        row += 18;
    }

    addItem(4, (short)row, 140, 8, IDC_STATIC_ENC, SS_LEFT, 0, L"STATIC", L"Filename encoding:");

    // Трохи збільшив висоту списку кодувань, щоб він нормально випадав
    addItem(148, (short)row, 100, 40, IDC_COMBO_ENC,
        CBS_DROPDOWNLIST | WS_TABSTOP, 0, L"COMBOBOX", L"");
    row += 18;

    addItem(4, (short)row, 180, 10, IDC_CHK_READONLY,
        BS_AUTOCHECKBOX | WS_TABSTOP, 0, L"BUTTON", L"Read-only access");
    row += 18;

    addItem(4, (short)row, 250, 8, IDC_STATIC_INFO, SS_LEFT, 0, L"STATIC",
        isMount ? L"Tip: offset 0 = whole disk / first partition"
        : L"Settings apply to newly mounted volumes");
    row += 24;

    addItem(190, (short)row, 50, 14, IDOK,
        BS_DEFPUSHBUTTON | WS_TABSTOP, 0, L"BUTTON", isMount ? L"Mount" : L"OK");
    addItem(246, (short)row, 50, 14, IDCANCEL,
        BS_PUSHBUTTON | WS_TABSTOP, 0, L"BUTTON", L"Cancel");

    GlobalUnlock(hMem);
    return hMem;
}

bool ShowConfigDialog(HWND parent, PluginConfig& cfg) {
    MountDlgData data{ &cfg, true };
    HGLOBAL hTmpl = BuildDialogTemplate(false);
    if (!hTmpl) return false;

    // ВИПРАВЛЕННЯ: Перевірка на NULL для GlobalLock
    DLGTEMPLATE* pDlgTmpl = (DLGTEMPLATE*)GlobalLock(hTmpl);
    if (!pDlgTmpl) {
        GlobalFree(hTmpl);
        return false;
    }

    INT_PTR result = DialogBoxIndirectParamA(
        GetModuleHandleA(nullptr),
        pDlgTmpl,
        parent, MountDlgProc, (LPARAM)&data);

    GlobalUnlock(hTmpl);
    GlobalFree(hTmpl);
    return result == IDOK;
}

bool ShowMountDialog(HWND parent, PluginConfig& cfg) {
    MountDlgData data{ &cfg, false };
    HGLOBAL hTmpl = BuildDialogTemplate(true);
    if (!hTmpl) return false;

    // ВИПРАВЛЕННЯ: Перевірка на NULL для GlobalLock
    DLGTEMPLATE* pDlgTmpl = (DLGTEMPLATE*)GlobalLock(hTmpl);
    if (!pDlgTmpl) {
        GlobalFree(hTmpl);
        return false;
    }

    INT_PTR result = DialogBoxIndirectParamA(
        GetModuleHandleA(nullptr),
        pDlgTmpl,
        parent, MountDlgProc, (LPARAM)&data);

    GlobalUnlock(hTmpl);
    GlobalFree(hTmpl);
    return result == IDOK;
}