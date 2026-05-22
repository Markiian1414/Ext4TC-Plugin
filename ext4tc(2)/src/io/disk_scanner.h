#pragma once
#ifndef DISK_SCANNER_H
#define DISK_SCANNER_H

#include <string>
#include <vector>
#include <cstdint>

// -------------------------------------------------------
//  Описує один знайдений Ext2/3/4-розділ на фізичному диску
// -------------------------------------------------------
struct FoundPartition {
    std::string  drivePath;      // наприклад \\.\PhysicalDrive1
    std::string  displayName;    // наприклад "PhysicalDrive1 Part2 (ext4, 16 GB)"
    uint64_t     byteOffset;     // зміщення розділу від початку диска в байтах
    uint64_t     byteSize;       // розмір розділу в байтах
    uint8_t      partType;       // тип розділу з MBR (0x83) або GPT GUID-ознака
    bool         isGpt;          // true = GPT, false = MBR
    int          driveIndex;     // 0..N — індекс PhysicalDrive
    int          partIndex;      // 1-based номер розділу
};

// -------------------------------------------------------
//  Головна функція: сканує PhysicalDrive0..maxDrives-1,
//  читає MBR або GPT, повертає всі Ext2/3/4-розділи.
//  Потребує прав адміністратора для відкриття \\.\PhysicalDriveN.
// -------------------------------------------------------
std::vector<FoundPartition> ScanForExtPartitions(int maxDrives = 16);

#endif // DISK_SCANNER_H