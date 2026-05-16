#include "plugin_state.h"
#include "io/disk_source.h"
#include "io/disk_scanner.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "shlwapi.lib")

extern HMODULE g_hModule;

PluginState& PluginState::Get() {
    static PluginState inst;
    return inst;
}

PluginState::PluginState()
    : pluginNr(0), progressProc(nullptr), logProc(nullptr),
    requestProc(nullptr), defaultReadOnly(true),
    defaultEncoding("UTF-8"), m_nextHandle(1)
{
}

void PluginState::LoadConfig() {
    wchar_t dllPath[MAX_PATH]{};
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);
    PathRemoveFileSpecW(dllPath);

    wchar_t ini[MAX_PATH]{};
    PathCombineW(ini, dllPath, L"ext4tc.ini");

    int n = WideCharToMultiByte(CP_UTF8, 0, ini, -1, nullptr, 0, nullptr, nullptr);
    configPath.resize(n - 1);
    WideCharToMultiByte(CP_UTF8, 0, ini, -1, configPath.data(), n, nullptr, nullptr);

    char buf[256]{};
    GetPrivateProfileStringA("General", "ReadOnly", "1", buf, sizeof(buf), configPath.c_str());
    defaultReadOnly = (buf[0] == '1');

    GetPrivateProfileStringA("General", "Encoding", "UTF-8", buf, sizeof(buf), configPath.c_str());
    defaultEncoding = buf;
}

void PluginState::SaveConfig() {
    WritePrivateProfileStringA("General", "ReadOnly",
        defaultReadOnly ? "1" : "0", configPath.c_str());
    WritePrivateProfileStringA("General", "Encoding",
        defaultEncoding.c_str(), configPath.c_str());
}

bool PluginState::MountVolume(const std::string& sourcePath,
    uint64_t partOffset,
    bool readOnly,
    std::string& errorMsg) {
    std::lock_guard<std::mutex> lk(mutex);

    // Конвертуємо унікальний вказівник у спільний (shared_ptr) для передачі в Mount
    std::shared_ptr<IDiskSource> src = CreateDiskSource(sourcePath, readOnly);
    if (!src) { errorMsg = "Cannot open source: " + sourcePath; return false; }

    auto reader = std::make_shared<ExtReader>();
    if (!reader->Mount(src, partOffset)) {
        errorMsg = "Not a valid Ext2/3/4 filesystem";
        return false;
    }

    MountedVolume vol;
    vol.sourcePath = sourcePath;
    vol.partOffset = partOffset;
    vol.readOnly = readOnly;
    vol.source = src;
    vol.reader = reader;

    const char* vname = reader->GetFsInfo().volume_name;
    if (vname[0])
        vol.label = vname;
    else {
        size_t slash = sourcePath.find_last_of("\\/");
        std::string fname = (slash != std::string::npos)
            ? sourcePath.substr(slash + 1) : sourcePath;
        size_t dot = fname.rfind('.');
        vol.label = (dot != std::string::npos) ? fname.substr(0, dot) : fname;
    }
    vol.id = "vol" + std::to_string(volumes.size());
    volumes.push_back(std::move(vol));
    return true;
}

void PluginState::UnmountAll() {
    std::lock_guard<std::mutex> lk(mutex);
    for (auto& v : volumes) v.reader->Unmount();
    volumes.clear();
}

void PluginState::ScanDisks() {
    // Запускаємо в поточному потоці; TC викликає FsFindFirst з UI-потоку,
    // тому блокування на ~1 секунду допустиме (16 дисків * ~30 мс = ~0.5 с)
    auto found = ScanForExtPartitions(16);
    std::lock_guard<std::mutex> lk(mutex);
    m_scannedPartitions = std::move(found);
    Log(MSGTYPE_DETAILS,
        "ext4tc: disk scan found " +
        std::to_string(m_scannedPartitions.size()) + " Ext partition(s)");
}

MountedVolume* PluginState::FindVolume(const std::string& id) {
    for (auto& v : volumes)
        if (v.id == id) return &v;
    return nullptr;
}

bool PluginState::ParsePath(const std::string& tcPath,
    std::string& volumeId,
    std::string& subPath) {
    std::string p = tcPath;
    std::replace(p.begin(), p.end(), '\\', '/');
    while (!p.empty() && p[0] == '/') p = p.substr(1);

    if (p.substr(0, 6) == "ext4tc") p = p.substr(6);
    while (!p.empty() && p[0] == '/') p = p.substr(1);

    if (p.empty()) {
        volumeId = ""; subPath = "/"; return true;
    }

    size_t slash = p.find('/');
    if (slash == std::string::npos) {
        // Якщо шляху після vol0 немає, значить ми в корені цього тому
        volumeId = p;
        subPath = "/";
        return true;
    }
    volumeId = p.substr(0, slash);
    subPath = p.substr(slash); // наприклад, "/home/testuser"
    return true;
}

HANDLE PluginState::AllocFindHandle(FindHandle&& fh) {
    LONG id = InterlockedIncrement(&m_nextHandle);
    HANDLE h = (HANDLE)(intptr_t)id;
    m_findHandles[h] = std::move(fh);
    return h;
}

FindHandle* PluginState::GetFindHandle(HANDLE h) {
    auto it = m_findHandles.find(h);
    return (it != m_findHandles.end()) ? &it->second : nullptr;
}

void PluginState::FreeFindHandle(HANDLE h) {
    m_findHandles.erase(h);
}

void PluginState::Log(int msgType, const std::string& msg) {
    if (logProc)
        logProc(pluginNr, msgType, const_cast<char*>(msg.c_str()));
}