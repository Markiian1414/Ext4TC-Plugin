#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <string>
#include "config_dialog.h"
#include "../wfx/plugin_state.h"

#pragma comment(lib, "comdlg32.lib")

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

struct MountDlgData {
    PluginConfig* cfg;
    bool          isConfig; // true = ShowConfigDialog (без Path/Browse)
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
            ofn.lpstrFilter =
                "Disk images (*.img;*.bin;*.raw)\0*.img;*.bin;*.raw\0"
                "All files (*.*)\0*.*\0";
            ofn.lpstrFile = fileBuf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            ofn.lpstrTitle = "Select Ext2/3/4 disk image";
            if (GetOpenFileNameA(&ofn))
                SetDlgItemTextA(hDlg, IDC_EDIT_PATH, fileBuf);
        }
        else if (id == IDOK) {
            if (!data->isConfig) {
                char buf[MAX_PATH]{};
                GetDlgItemTextA(hDlg, IDC_EDIT_PATH, buf, MAX_PATH);
                data->cfg->mountPath = buf;
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
    // Mount: 6 контролів (label + edit + browse + checkbox + OK + Cancel)
    // Config: 3 контролів (checkbox + OK + Cancel)
    const short itemCount = isMount ? 6 : 3;
    const short dlgH = isMount ? 78 : 58;

    HGLOBAL hMem = GlobalAlloc(GMEM_ZEROINIT, 4096);
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
    const wchar_t* title = isMount ? L"Mount Ext2/3/4 Volume"
        : L"Ext4TC Plugin Settings";
    wcscpy((wchar_t*)p, title);
    p += wcslen(title) + 1;

    // 9pt Segoe UI
    *p++ = 9;
    wcscpy((wchar_t*)p, L"Segoe UI");
    p += wcslen(L"Segoe UI") + 1;
    if ((uintptr_t)p & 2) p++;

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

    // --- Розрахунок координат ---
    //  Робоча ширина: DLG_W - 2*DLG_PAD = 296
    //  Browse: ширина 54, притиснутий до правого краю
    //  Path edit: від (PAD + labelW + 4) до лівого краю Browse мінус 4
    //  Кнопки: правий Cancel впритул до правого краю, Mount зліва від нього

    const short PAD = DLG_PAD;
    const short INNER = DLG_W - 2 * PAD;   // 296

    const short BROWSE_W = 54;
    const short BROWSE_X = DLG_W - PAD - BROWSE_W;         // 254
    const short LABEL_W = 32;
    const short EDIT_X = PAD + LABEL_W + 4;              //  48
    const short EDIT_W = BROWSE_X - EDIT_X - 4;          // 202

    // кнопки вирівняні по правому краю
    const short BTN2_X = DLG_W - PAD - DLG_BTN_W;       // 258
    const short BTN1_X = BTN2_X - DLG_BTN_GAP - DLG_BTN_W; // 202

    if (isMount) {
        // Рядок 1 — Path (y=14)
        const short R1 = DLG_FIRST_ROW;
        // лейбл центруємо по висоті відносно edit (h=12): зміщення +2
        addItem(PAD, R1 + 2, LABEL_W, 8,
            IDC_STATIC_PATH,
            SS_LEFT | SS_CENTERIMAGE, 0,
            L"STATIC", L"Path:");
        addItem(EDIT_X, R1, EDIT_W, 12,
            IDC_EDIT_PATH,
            WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
            WS_EX_CLIENTEDGE,
            L"EDIT", L"");
        addItem(BROWSE_X, R1, BROWSE_W, 12,
            IDC_BTN_BROWSE,
            BS_PUSHBUTTON | WS_TABSTOP, 0,
            L"BUTTON", L"Browse...");

        // Рядок 2 — Read-only (y=34)
        const short R2 = R1 + DLG_ROW_H;
        addItem(PAD, R2, INNER, 10,
            IDC_CHK_READONLY,
            BS_AUTOCHECKBOX | WS_TABSTOP, 0,
            L"BUTTON", L"Read-only access");

        // Рядок 3 — кнопки (y=50)
        const short R3 = R2 + DLG_ROW_H - 4;
        addItem(BTN1_X, R3, DLG_BTN_W, DLG_BTN_H,
            IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP, 0,
            L"BUTTON", L"Mount");
        addItem(BTN2_X, R3, DLG_BTN_W, DLG_BTN_H,
            IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP, 0,
            L"BUTTON", L"Cancel");
    }
    else {
        // Рядок 1 — Read-only (y=14)
        const short R1 = DLG_FIRST_ROW;
        addItem(PAD, R1, INNER, 10,
            IDC_CHK_READONLY,
            BS_AUTOCHECKBOX | WS_TABSTOP, 0,
            L"BUTTON", L"Read-only access (default for new mounts)");

        // Рядок 2 — кнопки (y=32)
        const short R2 = R1 + DLG_ROW_H - 2;
        addItem(BTN1_X, R2, DLG_BTN_W, DLG_BTN_H,
            IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP, 0,
            L"BUTTON", L"OK");
        addItem(BTN2_X, R2, DLG_BTN_W, DLG_BTN_H,
            IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP, 0,
            L"BUTTON", L"Cancel");
    }

    GlobalUnlock(hMem);
    return hMem;
}

// -------------------------------------------------------
//  Публічні функції
// -------------------------------------------------------

bool ShowConfigDialog(HWND parent, PluginConfig& cfg)
{
    MountDlgData data{ &cfg, true };
    HGLOBAL hTmpl = BuildDialogTemplate(false);
    if (!hTmpl) return false;

    auto* pTmpl = (DLGTEMPLATE*)GlobalLock(hTmpl);
    if (!pTmpl) { GlobalFree(hTmpl); return false; }

    INT_PTR r = DialogBoxIndirectParamA(
        GetModuleHandleA(nullptr), pTmpl,
        parent, MountDlgProc, (LPARAM)&data);

    GlobalUnlock(hTmpl);
    GlobalFree(hTmpl);
    return r == IDOK;
}

bool ShowMountDialog(HWND parent, PluginConfig& cfg)
{
    MountDlgData data{ &cfg, false };
    HGLOBAL hTmpl = BuildDialogTemplate(true);
    if (!hTmpl) return false;

    auto* pTmpl = (DLGTEMPLATE*)GlobalLock(hTmpl);
    if (!pTmpl) { GlobalFree(hTmpl); return false; }

    INT_PTR r = DialogBoxIndirectParamA(
        GetModuleHandleA(nullptr), pTmpl,
        parent, MountDlgProc, (LPARAM)&data);

    GlobalUnlock(hTmpl);
    GlobalFree(hTmpl);
    return r == IDOK;
}