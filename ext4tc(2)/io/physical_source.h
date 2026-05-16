#pragma once
#ifndef PHYSICAL_SOURCE_H
#define PHYSICAL_SOURCE_H

#include "disk_source.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class PhysicalDiskSource : public IDiskSource {
public:
    PhysicalDiskSource();
    ~PhysicalDiskSource() override;
    bool        Open(const std::string& path, bool readOnly = true) override;
    void        Close() override;
    bool        IsOpen()     const override;
    bool        IsReadOnly() const override;
    uint64_t    GetSizeBytes() const override;
    int64_t     ReadBytes(uint64_t byteOffset, void* buffer, uint32_t count) override;
    int64_t     WriteBytes(uint64_t byteOffset, const void* buffer, uint32_t count) override;
    std::string GetDescription() const override;
    uint32_t    GetSectorSize() const { return m_sectorSize; }
private:
    HANDLE   m_handle;
    bool     m_readOnly;
    uint64_t m_sizeBytes;
    uint32_t m_sectorSize;
    std::string m_path;
};

#endif
