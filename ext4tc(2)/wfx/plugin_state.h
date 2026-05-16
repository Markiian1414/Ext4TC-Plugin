#pragma once
#ifndef PLUGIN_STATE_H
#define PLUGIN_STATE_H

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <windows.h>
#include "../ext/ext_reader.h"
#include "../io/disk_scanner.h"
#include "wfxplugin.h"

// -------------------------------------------------------
//  Збережені налаштування
// -------------------------------------------------------
struct PluginConfig {
    std::string mountPath;
    uint64_t    partOffset = 0; // Ініціалізовано
    bool        readOnly = true; // Ініціалізовано
    std::string encoding = "UTF-8";
};

// -------------------------------------------------------
//  Змонтований том
// -------------------------------------------------------
struct MountedVolume {
    std::string id;
    std::string label;
    std::string sourcePath;
    uint64_t partOffset = 0; // ВИПРАВЛЕНО: ініціалізація
    bool readOnly = true;    // ВИПРАВЛЕНО: ініціалізація
    std::shared_ptr<IDiskSource> source;
    std::shared_ptr<ExtReader> reader;
};

// -------------------------------------------------------
//  Стан відкритої папки (для FsFindFirst / FsFindNext)
// -------------------------------------------------------
struct FindHandle {
    std::vector<ExtEntry> entries;
    size_t pos = 0;       // ВИПРАВЛЕНО: ініціалізація
    bool valid = false;   // ВИПРАВЛЕНО: ініціалізація
};

// -------------------------------------------------------
//  Глобальний стан плагіна (Singleton)
// -------------------------------------------------------
class PluginState {
public:
    static PluginState& Get();

    int pluginNr;
    tProgressProc progressProc;
    tLogProc logProc;
    tRequestProc requestProc;

    std::mutex mutex;
    std::vector<MountedVolume> volumes;
    std::string configPath;

    bool defaultReadOnly;
    std::string defaultEncoding;

    void LoadConfig();
    void SaveConfig();

    bool MountVolume(const std::string& sourcePath, uint64_t partOffset,
        bool readOnly, std::string& errorMsg);
    void UnmountAll();

    // Сканування фізичних дисків на Ext-розділи (MBR + GPT)
    void ScanDisks();
    const std::vector<FoundPartition>& GetScannedPartitions() const { return m_scannedPartitions; }

    MountedVolume* FindVolume(const std::string& id);

    bool ParsePath(const std::string& tcPath, std::string& volumeId, std::string& subPath);

    HANDLE AllocFindHandle(FindHandle&& fh);
    FindHandle* GetFindHandle(HANDLE h);
    void FreeFindHandle(HANDLE h);

    void Log(int msgType, const std::string& msg);

private:
    PluginState();
    ~PluginState() = default;

    std::unordered_map<HANDLE, FindHandle> m_findHandles;
    volatile LONG m_nextHandle;
    std::vector<FoundPartition> m_scannedPartitions;
};

#endif // PLUGIN_STATE_H