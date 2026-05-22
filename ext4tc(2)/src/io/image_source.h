#pragma once
#ifndef IMAGE_SOURCE_H
#define IMAGE_SOURCE_H

#include "disk_source.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class ImageFileSource : public IDiskSource {
public:
    ImageFileSource();
    ~ImageFileSource() override;
    bool        Open(const std::string& path, bool readOnly = true) override;
    void        Close() override;
    bool        IsOpen()     const override;
    bool        IsReadOnly() const override;
    uint64_t    GetSizeBytes() const override;
    int64_t     ReadBytes(uint64_t byteOffset, void* buffer, uint32_t count) override;
    int64_t     WriteBytes(uint64_t byteOffset, const void* buffer, uint32_t count) override;
    std::string GetDescription() const override;
private:
    HANDLE      m_handle;
    bool        m_readOnly;
    uint64_t    m_sizeBytes;
    std::string m_path;
};

#endif
