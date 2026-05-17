#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <string>

#include "wfx/wfxplugin.h"
#include "plugin_state.h"
#include "gui/config_dialog.h"

// --- Допоміжні функції ---

static bool IsAdmin() {
    return IsUserAnAdmin() != FALSE;
}

// ДОДАНО: Допоміжна функція для витягування імені файлу зі шляху
static std::string GetFilenameFromPath(const std::string& path) {
    size_t found = path.find_last_of("/\\");
    if (found != std::string::npos) {
        return path.substr(found + 1);
    }
    return path;
}

static FILETIME UnixToFT(uint32_t t) {
    LONGLONG ll = (LONGLONG)t * 10000000LL + 116444736000000000LL;
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(ll & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(ll >> 32);
    return ft;
}

static void FillFindData(const ExtEntry& e, WIN32_FIND_DATAA* fd) {
    memset(fd, 0, sizeof(*fd));
    strcpy_s(fd->cFileName, sizeof(fd->cFileName), e.name.c_str());

    fd->dwFileAttributes = e.is_dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (e.is_symlink)
        fd->dwFileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;

    fd->dwReserved0 = e.mode & 0x0FFF;
    fd->nFileSizeLow = (DWORD)(e.size & 0xFFFFFFFF);
    fd->nFileSizeHigh = (DWORD)(e.size >> 32);

    if (e.mtime == 0) {
        GetSystemTimeAsFileTime(&fd->ftLastWriteTime);
        fd->ftCreationTime = fd->ftLastWriteTime;
        fd->ftLastAccessTime = fd->ftLastWriteTime;
    }
    else {
        fd->ftLastWriteTime = UnixToFT((uint32_t)e.mtime);
        fd->ftLastAccessTime = UnixToFT((uint32_t)e.atime);
        fd->ftCreationTime = UnixToFT((uint32_t)e.ctime);
    }
}

// --- Основні функції API ---

int __stdcall FsInit(int PluginNr, tProgressProc pProgressProc, tLogProc pLogProc, tRequestProc pRequestProc) {
    auto& ps = PluginState::Get();
    ps.pluginNr = PluginNr;
    ps.progressProc = pProgressProc;
    ps.logProc = pLogProc;
    ps.requestProc = pRequestProc;
    ps.LoadConfig();
    return 0;
}

void __stdcall FsGetDefRootName(char* DefRootName, int maxlen) {
    strncpy_s(DefRootName, maxlen, "Ext2/3/4 Filesystem", maxlen - 1);
}

HANDLE __stdcall FsFindFirst(char* Path, WIN32_FIND_DATAA* FindData) {
    auto& ps = PluginState::Get();
    std::string volumeId, subPath;
    ps.ParsePath(Path ? Path : "", volumeId, subPath);

    static bool adminWarningShown = false;
    if (volumeId.empty() && !adminWarningShown) {
        if (!IsAdmin()) {
            MessageBoxA(NULL,
                "Warning: Plugin is not running as Administrator.\nPhysical drive auto-detection is disabled.",
                "ext4tc", MB_ICONWARNING | MB_OK);
        }
        adminWarningShown = true;
    }

    FindHandle fh;
    fh.pos = 0;
    fh.valid = true;

    if (volumeId.empty()) {
        ps.ScanDisks();
        std::lock_guard<std::mutex> lk(ps.mutex);

        // 1. Автоматично знайдені Ext-розділи
        const auto& partitions = ps.GetScannedPartitions();
        for (const auto& fp : partitions) {
            ExtEntry e{};
            char id[32];
            _snprintf_s(id, sizeof(id), _TRUNCATE, "disk%dp%d", fp.driveIndex, fp.partIndex);
            e.name = id;
            e.is_dir = true;
            e.mtime = 0;
            fh.entries.push_back(e);
        }

        // 2. Вручну змонтовані томи (БЕЗ ДУБЛЮВАННЯ)
        for (auto& v : ps.volumes) {
            bool alreadyExists = false;
            for (const auto& existing : fh.entries) {
                if (existing.name == v.id) { alreadyExists = true; break; }
            }
            if (!alreadyExists) {
                ExtEntry e{};
                e.name = v.id;
                e.is_dir = true;
                e.mtime = 0;
                fh.entries.push_back(e);
            }
        }

        // 3. Кнопка ручного монтування
        ExtEntry mountEntry{};
        mountEntry.name = L10n::S("mount_new_entry"); // <--- ВИКОРИСТОВУЄМО lang.h
        mountEntry.is_dir = false;
        mountEntry.mtime = 0;
        fh.entries.push_back(mountEntry);
    }
    else {
        std::string targetDrivePath = "";
        uint64_t targetByteOffset = 0;
        bool needsMount = false;

        {
            std::lock_guard<std::mutex> lk(ps.mutex);
            if (ps.FindVolume(volumeId) == nullptr) {
                for (const auto& fp : ps.GetScannedPartitions()) {
                    char id[32];
                    _snprintf_s(id, sizeof(id), _TRUNCATE, "disk%dp%d", fp.driveIndex, fp.partIndex);
                    if (volumeId == id) {
                        targetDrivePath = fp.drivePath;
                        targetByteOffset = fp.byteOffset;
                        needsMount = true;
                        break;
                    }
                }
            }
        }

        if (needsMount) {
            std::string err;
            if (ps.MountVolume(targetDrivePath, targetByteOffset, true, err)) {
                std::lock_guard<std::mutex> lk2(ps.mutex);
                if (!ps.volumes.empty()) ps.volumes.back().id = volumeId;
            }
            else {
                std::string errorMsg = "Failed to auto-mount:\n" + err;
                MessageBoxA(NULL, errorMsg.c_str(), "ext4tc — Mount Error", MB_ICONERROR | MB_OK);
            }
        }

        std::lock_guard<std::mutex> lk(ps.mutex);
        MountedVolume* vol = ps.FindVolume(volumeId);
        if (!vol || !vol->reader->ListDirectory(subPath, fh.entries)) return INVALID_HANDLE_VALUE;
    }

    if (fh.entries.empty()) {
        memset(FindData, 0, sizeof(*FindData));
        strcpy_s(FindData->cFileName, ".");
        FindData->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        return ps.AllocFindHandle(std::move(fh));
    }

    FillFindData(fh.entries[0], FindData);
    fh.pos = 1;
    return ps.AllocFindHandle(std::move(fh));
}

BOOL __stdcall FsFindNext(HANDLE Hdl, WIN32_FIND_DATAA* FindData) {
    auto& ps = PluginState::Get();
    FindHandle* fh = ps.GetFindHandle(Hdl);
    if (!fh || fh->pos >= fh->entries.size()) return FALSE;
    FillFindData(fh->entries[fh->pos++], FindData);
    return TRUE;
}

int __stdcall FsFindClose(HANDLE Hdl) {
    PluginState::Get().FreeFindHandle(Hdl);
    return 0;
}
int __stdcall FsExecuteFile(HWND MainWin, char* RemoteName, char* Verb) {
    auto& ps = PluginState::Get();
    std::string remote = RemoteName ? RemoteName : "";
    std::string verb = Verb ? Verb : "";

    // Перевіряємо обидва варіанти (на випадок якщо мова щойно змінилася)
    if (remote.find(L10n::S("mount_new_entry")) != std::string::npos ||
        remote.find("< Mount new volume >") != std::string::npos) {

        PluginConfig cfg;
        cfg.readOnly = ps.defaultReadOnly;
        cfg.encoding = ps.defaultEncoding;

        if (ShowMountDialog(MainWin, cfg)) {
            std::string err;
            if (ps.MountVolume(cfg.mountPath, cfg.partOffset, cfg.readOnly, err)) {

                // ВИПРАВЛЕНО: Присвоюємо змонтованому тому назву самого файлу
                {
                    std::lock_guard<std::mutex> lk(ps.mutex);
                    if (!ps.volumes.empty()) {
                        ps.volumes.back().id = GetFilenameFromPath(cfg.mountPath);
                    }
                }

                // Симуляція натискання F2 для миттєвого оновлення списку
                INPUT ip;
                ip.type = INPUT_KEYBOARD;
                ip.ki.wScan = 0;
                ip.ki.time = 0;
                ip.ki.dwExtraInfo = 0;
                ip.ki.wVk = VK_F2;
                ip.ki.dwFlags = 0;
                SendInput(1, &ip, sizeof(INPUT));
                ip.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &ip, sizeof(INPUT));

                return FS_EXEC_OK;
            }
            MessageBoxA(MainWin, err.c_str(), "ext4tc \x97 Mount Error", MB_ICONERROR | MB_OK);
        }
        return FS_EXEC_OK;
    }

    if (verb == "properties") {
        std::string volId, sub; ps.ParsePath(remote, volId, sub);

        // 1. Якщо це корінь плагіна (користувач натиснув Alt+Enter на [Ext2/3/4 Filesystem])
        if (volId.empty() && sub == "/") {
            PluginConfig cfg;
            cfg.readOnly = ps.defaultReadOnly;
            cfg.encoding = ps.defaultEncoding;

            if (ShowConfigDialog(MainWin, cfg)) {
                // Зберігаємо налаштування
                ps.defaultReadOnly = cfg.readOnly;
                ps.defaultEncoding = cfg.encoding;
                ps.SaveConfig();
            }
            return FS_EXEC_OK;
        }

        // 2. Якщо це конкретний файл/папка всередині змонтованого тому
        MountedVolume* vol = ps.FindVolume(volId);
        if (vol) {
            ext2_inode inode{}; uint32_t inum = 0;
            if (vol->reader->GetInodeByPath(sub, inode, inum)) {
                char info[512];
                uint64_t sz = inode.i_size_lo | ((uint64_t)inode.i_size_hi << 32);
                sprintf_s(info, sizeof(info), "Path: %s\nInode: %u\nSize: %llu bytes\n", sub.c_str(), inum, (unsigned long long)sz);
                // Локалізував заодно заголовок вікна
                MessageBoxA(MainWin, info, L10n::S("prop_title"), MB_ICONINFORMATION | MB_OK);
            }
        }
        return FS_EXEC_OK;
    }
    return FS_EXEC_YOURSELF;
}

// --- Функції роботи з файлами ---

int __stdcall FsGetFile(char* RemoteName, char* LocalName, int CopyFlags, RemoteInfoStruct* ri) {
    auto& ps = PluginState::Get();
    std::string volId, subPath;
    ps.ParsePath(RemoteName, volId, subPath);
    MountedVolume* vol = ps.FindVolume(volId);
    if (!vol) return FS_FILE_NOTFOUND;

    auto progress = [&](uint64_t d, uint64_t t) -> bool {
        if (!ps.progressProc) return true;
        return ps.progressProc(ps.pluginNr, RemoteName, LocalName, t ? (int)(d * 100 / t) : 100) == 0;
        };
    return vol->reader->ExtractFile(subPath, LocalName, progress) ? FS_FILE_OK : FS_FILE_READERROR;
}

int __stdcall FsPutFile(char* LocalName, char* RemoteName, int CopyFlags) {
    auto& ps = PluginState::Get();
    std::string volId, subPath;
    ps.ParsePath(RemoteName, volId, subPath);
    MountedVolume* vol = ps.FindVolume(volId);
    if (!vol || vol->readOnly) return FS_FILE_NOTSUPPORTED;
    return vol->reader->WriteFile(LocalName, subPath) ? FS_FILE_OK : FS_FILE_WRITEERROR;
}

BOOL __stdcall FsDeleteFile(char* RemoteName) {
    auto& ps = PluginState::Get();
    std::string volId, subPath;
    ps.ParsePath(RemoteName, volId, subPath);
    MountedVolume* vol = ps.FindVolume(volId);
    return (vol && !vol->readOnly && vol->reader->RemoveFile(subPath));
}

BOOL __stdcall FsRemoveDir(char* RemoteName) { return FsDeleteFile(RemoteName); }

BOOL __stdcall FsMkDir(char* Path) {
    auto& ps = PluginState::Get();
    std::string volId, subPath;
    ps.ParsePath(Path, volId, subPath);
    MountedVolume* vol = ps.FindVolume(volId);
    return (vol && !vol->readOnly && vol->reader->MakeDir(subPath));
}

int __stdcall FsRenMovFile(char* Old, char* New, BOOL M, BOOL O, RemoteInfoStruct* ri) { return FS_FILE_NOTSUPPORTED; }
BOOL __stdcall FsSetAttr(char* R, int A) { return FALSE; }
BOOL __stdcall FsSetTime(char* R, FILETIME* C, FILETIME* A, FILETIME* W) { return FALSE; }
void __stdcall FsStatusInfo(char* RemoteDir, int InfoStartEnd, int Operation) {
    // Total Commander викликає цю функцію на початку (InfoStartEnd=0)
    // і наприкінці (InfoStartEnd=1) кожної операції з файлами.
    // Використовуємо для логування стану фонових операцій.
    auto& ps = PluginState::Get();
    if (!ps.logProc) return;

    // Назви операцій для лога (відповідають константам WFX API)
    const char* opName = "Unknown";
    switch (Operation) {
    case 1:  opName = "List directory";  break;
    case 2:  opName = "Get file";        break;
    case 3:  opName = "Put file";        break;
    case 4:  opName = "Rename/Move";     break;
    case 5:  opName = "Delete";          break;
    case 6:  opName = "Attributes";      break;
    case 7:  opName = "Execute";         break;
    case 8:  opName = "Calculate size";  break;
    case 9:  opName = "Search";          break;
    case 10: opName = "Search text";     break;
    case 11: opName = "Synchronize";     break;
    }

    char msg[512];
    _snprintf_s(msg, sizeof(msg), _TRUNCATE,
        "ext4tc: %s — %s [%s]",
        opName,
        RemoteDir ? RemoteDir : "",
        InfoStartEnd == 0 ? "start" : "end");
    ps.Log(MSGTYPE_DETAILS, msg);
}
int __stdcall FsGetBackgroundFlags(void) { return BG_DOWNLOAD; }
BOOL __stdcall FsLinksToLocalFiles(void) { return FALSE; }


BOOL __stdcall FsContentGetDefaultView(char* VC, char* VH, char* VW, char* VO, int maxlen) {
    strncpy_s(VC, maxlen, "[=tc.size]\n[=tc.writedate]\n[tc.attr]", maxlen - 1);
    strncpy_s(VH, maxlen, "Size\nDate\nAttr", maxlen - 1);
    strncpy_s(VW, maxlen, "80\n100\n60", maxlen - 1);
    strncpy_s(VO, maxlen, "0", maxlen - 1);
    return TRUE;
}

// =======================================================
// UNICODE WRAPPERS (Дозволяють TC бачити кирилицю)
// =======================================================

static std::wstring Utf8ToWstr(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], wlen);
    return wstr;
}

static std::string WstrToUtf8(const wchar_t* wstr) {
    if (!wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    std::string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], len, nullptr, nullptr);
    return str;
}

extern "C" {

    __declspec(dllexport) HANDLE __stdcall FsFindFirstW(WCHAR* Path, WIN32_FIND_DATAW* FindData) {
        std::string utf8Path = WstrToUtf8(Path);
        WIN32_FIND_DATAA fdA{};
        HANDLE h = FsFindFirst((char*)utf8Path.c_str(), &fdA);
        if (h != INVALID_HANDLE_VALUE) {
            memset(FindData, 0, sizeof(*FindData));
            std::wstring wName = Utf8ToWstr(fdA.cFileName);
            wcscpy_s(FindData->cFileName, MAX_PATH, wName.c_str());
            FindData->dwFileAttributes = fdA.dwFileAttributes;
            FindData->nFileSizeHigh = fdA.nFileSizeHigh;
            FindData->nFileSizeLow = fdA.nFileSizeLow;
            FindData->ftCreationTime = fdA.ftCreationTime;
            FindData->ftLastAccessTime = fdA.ftLastAccessTime;
            FindData->ftLastWriteTime = fdA.ftLastWriteTime;
        }
        return h;
    }

    __declspec(dllexport) BOOL __stdcall FsFindNextW(HANDLE Hdl, WIN32_FIND_DATAW* FindData) {
        WIN32_FIND_DATAA fdA{};
        BOOL res = FsFindNext(Hdl, &fdA);
        if (res) {
            memset(FindData, 0, sizeof(*FindData));
            std::wstring wName = Utf8ToWstr(fdA.cFileName);
            wcscpy_s(FindData->cFileName, MAX_PATH, wName.c_str());
            FindData->dwFileAttributes = fdA.dwFileAttributes;
            FindData->nFileSizeHigh = fdA.nFileSizeHigh;
            FindData->nFileSizeLow = fdA.nFileSizeLow;
            FindData->ftCreationTime = fdA.ftCreationTime;
            FindData->ftLastAccessTime = fdA.ftLastAccessTime;
            FindData->ftLastWriteTime = fdA.ftLastWriteTime;
        }
        return res;
    }

    __declspec(dllexport) int __stdcall FsExecuteFileW(HWND MainWin, WCHAR* RemoteName, WCHAR* Verb) {
        std::string remote = WstrToUtf8(RemoteName);
        std::string verb = WstrToUtf8(Verb);
        return FsExecuteFile(MainWin, (char*)remote.c_str(), (char*)verb.c_str());
    }

} // кінець extern "C"