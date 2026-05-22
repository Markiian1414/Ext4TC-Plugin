#pragma once
#ifndef DISK_SOURCE_H
#define DISK_SOURCE_H

#include <cstdint>
#include <string>
#include <memory>

class IDiskSource {
public:
    virtual ~IDiskSource() = default;
    virtual bool        Open(const std::string& path, bool readOnly = true) = 0;
    virtual void        Close() = 0;
    virtual bool        IsOpen()     const = 0;
    virtual bool        IsReadOnly() const = 0;
    virtual uint64_t    GetSizeBytes() const = 0;
    virtual int64_t     ReadBytes(uint64_t byteOffset, void* buffer, uint32_t count) = 0;
    virtual int64_t     WriteBytes(uint64_t byteOffset, const void* buffer, uint32_t count) = 0;
    virtual std::string GetDescription() const = 0;

    bool ReadExact(uint64_t byteOffset, void* buffer, uint32_t count) {
        return ReadBytes(byteOffset, buffer, count) == (int64_t)count;
    }
};

std::unique_ptr<IDiskSource> CreateDiskSource(const std::string& path, bool readOnly = true);

#endif
