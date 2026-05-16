#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>
#include "disk_scanner.h"

// -------------------------------------------------------
//  Константи типів розділів
// -------------------------------------------------------

// MBR: Linux native filesystem (ext2/3/4, xfs, btrfs...)
static const uint8_t MBR_TYPE_LINUX = 0x83;

// GPT GUID для Linux Data (тип "0FC63DAF-8483-4772-8E79-3D69D8477DE4")
// Зберігаємо як 16 байт little-endian
static const uint8_t GPT_LINUX_DATA_GUID[16] = {
    0xAF, 0x3D, 0xC6, 0x0F,   // time_low (reversed)
    0x83, 0x84,                // time_mid (reversed)
    0x72, 0x47,                // time_hi_and_version (reversed)
    0x8E, 0x79,
    0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

// -------------------------------------------------------
//  MBR-структури (512 байт)
// -------------------------------------------------------
#pragma pack(push, 1)
struct MbrPartEntry {
    uint8_t  status;
    uint8_t  chsFirst[3];
    uint8_t  type;
    uint8_t  chsLast[3];
    uint32_t lbaStart;
    uint32_t lbaSize;
};

struct Mbr {
    uint8_t      bootstrap[446];
    MbrPartEntry partitions[4];
    uint16_t     signature;   // 0xAA55
};

// GPT Header (розташований в LBA 1)
struct GptHeader {
    char     signature[8];    // "EFI PART"
    uint32_t revision;
    uint32_t headerSize;
    uint32_t headerCrc32;
    uint32_t reserved;
    uint64_t myLba;
    uint64_t alternateLba;
    uint64_t firstUsableLba;
    uint64_t lastUsableLba;
    uint8_t  diskGuid[16];
    uint64_t partEntriesLba;
    uint32_t numPartEntries;
    uint32_t partEntrySize;
    uint32_t partArrayCrc32;
};

// GPT Partition Entry (зазвичай 128 байт)
struct GptPartEntry {
    uint8_t  typeGuid[16];
    uint8_t  uniqueGuid[16];
    uint64_t startLba;
    uint64_t endLba;
    uint64_t attributes;
    uint16_t name[36];   // UTF-16LE
};
#pragma pack(pop)

// -------------------------------------------------------
//  Допоміжні функції
// -------------------------------------------------------

// Відкриває фізичний диск тільки для читання (без буферизації)
static HANDLE OpenDrive(int index) {
    char path[32];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "\\\\.\\PhysicalDrive%d", index);
    return CreateFileA(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_RANDOM_ACCESS,
        nullptr
    );
}

// Читає рівно 512 байт з вирівняного LBA-сектора
static bool ReadSector(HANDLE hDrive, uint64_t lba, void* buf) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)(lba * 512ULL);
    if (!SetFilePointerEx(hDrive, li, nullptr, FILE_BEGIN))
        return false;
    DWORD got = 0;
    return ReadFile(hDrive, buf, 512, &got, nullptr) && got == 512;
}

// Читає count секторів по 512 байт, виділяє буфер через VirtualAlloc
// (VirtualAlloc завжди дає вирівняну сторінку — підходить для NO_BUFFERING)
static std::vector<uint8_t> ReadSectors(HANDLE hDrive, uint64_t lba, uint32_t count) {
    uint32_t bytes = count * 512;
    std::vector<uint8_t> result;

    void* buf = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) return result;

    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)(lba * 512ULL);
    bool ok = false;
    if (SetFilePointerEx(hDrive, li, nullptr, FILE_BEGIN)) {
        DWORD got = 0;
        ok = ReadFile(hDrive, buf, bytes, &got, nullptr) && got == bytes;
    }

    if (ok) {
        result.resize(bytes);
        memcpy(result.data(), buf, bytes);
    }
    VirtualFree(buf, 0, MEM_RELEASE);
    return result;
}

// Розмір диска в байтах
static uint64_t GetDriveSize(HANDLE hDrive) {
    DWORD ret = 0;
    DISK_GEOMETRY_EX geom{};
    if (DeviceIoControl(hDrive, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
        nullptr, 0, &geom, sizeof(geom), &ret, nullptr))
        return (uint64_t)geom.DiskSize.QuadPart;
    return 0;
}

// Форматує розмір у людино-читабельний рядок (KB / MB / GB)
static std::string FormatSize(uint64_t bytes) {
    char buf[32];
    if (bytes >= (1ULL << 30))
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.1f GB", bytes / (double)(1ULL << 30));
    else if (bytes >= (1ULL << 20))
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.0f MB", bytes / (double)(1ULL << 20));
    else
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.0f KB", bytes / (double)(1ULL << 10));
    return buf;
}

// Порівнює два GUID (16 байт)
static bool GuidEquals(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 16) == 0;
}

// Перевіряємо суперблок Ext2/3/4: magic = 0xEF53 за зміщенням 1080 від початку розділу
static bool IsExtSuperblock(HANDLE hDrive, uint64_t partStartLba) {
    // Суперблок знаходиться на байтовому зміщенні 1024 від початку розділу.
    // Для NO_BUFFERING читаємо цілий сектор (512 байт), але суперблок займає
    // 1024..2047 байт -> LBA partStartLba + 2 (байт 1024 = сектор 2 від початку розділу)
    // Однак, якщо сектор = 512 байт, то LBA 2 від початку розділу = байт 1024.
    // Magic word знаходиться за зміщенням 56 байт всередині суперблоку (= байт 1080 від розділу).
    // Тобто нам треба: partStartLba + 2 (сектор, що містить байт 1024-1535).
    // Offset всередині сектора: 1080 - 1024 = 56.

    uint64_t sbLba = partStartLba + 2; // байт 1024 від початку розділу
    uint8_t sector[512];
    if (!ReadSector(hDrive, sbLba, sector))
        return false;

    // magic = sector[56..57] (little-endian)
    uint16_t magic = (uint16_t)(sector[56] | (sector[57] << 8));
    return magic == 0xEF53;
}

// -------------------------------------------------------
//  Обробка MBR-диска
// -------------------------------------------------------
static void ScanMbr(HANDLE hDrive, int driveIndex,
    std::vector<FoundPartition>& out) {
    uint8_t sector[512];
    if (!ReadSector(hDrive, 0, sector)) return;

    const Mbr* mbr = reinterpret_cast<const Mbr*>(sector);
    if (mbr->signature != 0xAA55) return;

    for (int i = 0; i < 4; i++) {
        const MbrPartEntry& pe = mbr->partitions[i];
        if (pe.type != MBR_TYPE_LINUX) continue;
        if (pe.lbaStart == 0 || pe.lbaSize == 0) continue;

        uint64_t offset = (uint64_t)pe.lbaStart * 512ULL;
        uint64_t size = (uint64_t)pe.lbaSize * 512ULL;

        // Додаткова перевірка: чи дійсно там суперблок Ext?
        if (!IsExtSuperblock(hDrive, (uint64_t)pe.lbaStart)) continue;

        FoundPartition fp;
        fp.driveIndex = driveIndex;
        fp.partIndex = i + 1;
        fp.isGpt = false;
        fp.partType = pe.type;
        fp.byteOffset = offset;
        fp.byteSize = size;

        char drivePath[32];
        _snprintf_s(drivePath, sizeof(drivePath), _TRUNCATE,
            "\\\\.\\PhysicalDrive%d", driveIndex);
        fp.drivePath = drivePath;

        char name[64];
        _snprintf_s(name, sizeof(name), _TRUNCATE,
            "Drive%d Part%d [MBR, ext, %s]",
            driveIndex, i + 1, FormatSize(size).c_str());
        fp.displayName = name;

        out.push_back(fp);
    }
}

// -------------------------------------------------------
//  Обробка GPT-диска
// -------------------------------------------------------
static void ScanGpt(HANDLE hDrive, int driveIndex,
    const GptHeader& hdr, std::vector<FoundPartition>& out) {
    if (hdr.numPartEntries == 0 || hdr.partEntrySize < sizeof(GptPartEntry))
        return;
    if (hdr.numPartEntries > 256) return; // захист від битого заголовка

    // Читаємо масив записів розділів (зазвичай 128 * 128 = 16384 байт = 32 сектори)
    uint32_t totalBytes = hdr.numPartEntries * hdr.partEntrySize;
    uint32_t sectors = (totalBytes + 511) / 512;
    auto data = ReadSectors(hDrive, hdr.partEntriesLba, sectors);
    if (data.empty()) return;

    for (uint32_t i = 0; i < hdr.numPartEntries; i++) {
        const uint8_t* raw = data.data() + (size_t)i * hdr.partEntrySize;
        const GptPartEntry* pe = reinterpret_cast<const GptPartEntry*>(raw);

        // Пропускаємо незайняті записи (typeGuid = все нулі)
        bool empty = true;
        for (int b = 0; b < 16; b++) if (pe->typeGuid[b]) { empty = false; break; }
        if (empty) continue;

        if (!GuidEquals(pe->typeGuid, GPT_LINUX_DATA_GUID)) continue;
        if (pe->startLba == 0 || pe->endLba < pe->startLba) continue;

        uint64_t sizeLba = pe->endLba - pe->startLba + 1;
        uint64_t offset = pe->startLba * 512ULL;
        uint64_t size = sizeLba * 512ULL;

        // Перевіряємо суперблок
        if (!IsExtSuperblock(hDrive, pe->startLba)) continue;

        FoundPartition fp;
        fp.driveIndex = driveIndex;
        fp.partIndex = (int)(i + 1);
        fp.isGpt = true;
        fp.partType = 0x83; // умовно
        fp.byteOffset = offset;
        fp.byteSize = size;

        char drivePath[32];
        _snprintf_s(drivePath, sizeof(drivePath), _TRUNCATE,
            "\\\\.\\PhysicalDrive%d", driveIndex);
        fp.drivePath = drivePath;

        char name[64];
        _snprintf_s(name, sizeof(name), _TRUNCATE,
            "Drive%d Part%d [GPT, ext, %s]",
            driveIndex, (int)(i + 1), FormatSize(size).c_str());
        fp.displayName = name;

        out.push_back(fp);
    }
}

// -------------------------------------------------------
//  Головна функція сканування
// -------------------------------------------------------
std::vector<FoundPartition> ScanForExtPartitions(int maxDrives) {
    std::vector<FoundPartition> result;

    for (int i = 0; i < maxDrives; i++) {
        HANDLE hDrive = OpenDrive(i);
        if (hDrive == INVALID_HANDLE_VALUE) {
            // PhysicalDriveN не існує або немає доступу — пропускаємо
            continue;
        }

        uint8_t sector[512];
        if (!ReadSector(hDrive, 0, sector)) {
            CloseHandle(hDrive);
            continue;
        }

        // Перевіряємо: чи є GPT protective MBR + GPT header в LBA 1
        // GPT protective MBR має запис розділу з типом 0xEE
        const Mbr* mbr = reinterpret_cast<const Mbr*>(sector);
        bool hasProtectiveMbr = (mbr->signature == 0xAA55 &&
            mbr->partitions[0].type == 0xEE);

        if (hasProtectiveMbr) {
            // Спроба прочитати GPT Header з LBA 1
            uint8_t gptSector[512];
            if (ReadSector(hDrive, 1, gptSector)) {
                const GptHeader* gpt = reinterpret_cast<const GptHeader*>(gptSector);
                if (memcmp(gpt->signature, "EFI PART", 8) == 0) {
                    ScanGpt(hDrive, i, *gpt, result);
                    CloseHandle(hDrive);
                    continue;
                }
            }
        }

        // Звичайний MBR
        ScanMbr(hDrive, i, result);
        CloseHandle(hDrive);
    }

    return result;
}