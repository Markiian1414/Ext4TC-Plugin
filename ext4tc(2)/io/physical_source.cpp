#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include "physical_source.h"
#include <vector>

PhysicalDiskSource::PhysicalDiskSource()
    : m_handle(INVALID_HANDLE_VALUE), m_readOnly(true),
      m_sizeBytes(0), m_sectorSize(512) {}

PhysicalDiskSource::~PhysicalDiskSource() { Close(); }

bool PhysicalDiskSource::Open(const std::string& path, bool readOnly) {
    Close();
    m_readOnly = readOnly;
    m_path = path;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wp(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wp.data(), wlen);

    DWORD access = readOnly ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE;
    DWORD share  = FILE_SHARE_READ | FILE_SHARE_WRITE;

    m_handle = CreateFileW(wp.c_str(), access, share, nullptr,
                           OPEN_EXISTING,
                           FILE_FLAG_NO_BUFFERING | FILE_FLAG_RANDOM_ACCESS,
                           nullptr);
    if (m_handle == INVALID_HANDLE_VALUE) return false;

    DWORD ret = 0;
    DISK_GEOMETRY_EX geom{};
    if (DeviceIoControl(m_handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                        nullptr, 0, &geom, sizeof(geom), &ret, nullptr)) {
        m_sectorSize = geom.Geometry.BytesPerSector;
        m_sizeBytes  = (uint64_t)geom.DiskSize.QuadPart;
    } else {
        PARTITION_INFORMATION_EX pi{};
        if (DeviceIoControl(m_handle, IOCTL_DISK_GET_PARTITION_INFO_EX,
                            nullptr, 0, &pi, sizeof(pi), &ret, nullptr)) {
            m_sizeBytes = (uint64_t)pi.PartitionLength.QuadPart;
        } else {
            Close(); return false;
        }
    }
    return true;
}

void PhysicalDiskSource::Close() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
    m_sizeBytes = 0; m_sectorSize = 512;
}

bool     PhysicalDiskSource::IsOpen()     const { return m_handle != INVALID_HANDLE_VALUE; }
bool     PhysicalDiskSource::IsReadOnly() const { return m_readOnly; }
uint64_t PhysicalDiskSource::GetSizeBytes() const { return m_sizeBytes; }

int64_t PhysicalDiskSource::ReadBytes(uint64_t offset, void* buf, uint32_t count) {
    if (!IsOpen() || !buf || !count) return -1;

    uint64_t sector     = m_sectorSize;
    uint64_t alignedOff = (offset / sector) * sector;
    uint32_t skip       = (uint32_t)(offset - alignedOff);
    uint32_t total      = skip + count;
    uint32_t readSize   = (uint32_t)(((total + sector - 1) / sector) * sector);

    // VirtualAlloc gives sector-aligned memory
    void* tmp = VirtualAlloc(nullptr, readSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!tmp) return -1;

    LARGE_INTEGER li{}; li.QuadPart = (LONGLONG)alignedOff;
    SetFilePointerEx(m_handle, li, nullptr, FILE_BEGIN);

    DWORD got = 0;
    BOOL  ok  = ReadFile(m_handle, tmp, readSize, &got, nullptr);
    int64_t result = -1;
    if (ok && got >= skip + count) {
        memcpy(buf, (uint8_t*)tmp + skip, count);
        result = count;
    } else if (ok && got > skip) {
        uint32_t have = got - skip;
        memcpy(buf, (uint8_t*)tmp + skip, have);
        result = have;
    }
    VirtualFree(tmp, 0, MEM_RELEASE);
    return result;
}

int64_t PhysicalDiskSource::WriteBytes(uint64_t offset, const void* buf, uint32_t count) {
    if (m_readOnly || !IsOpen()) return -1;
    // Aligned-only write for safety
    if (offset % m_sectorSize || count % m_sectorSize) return -1;

    LARGE_INTEGER li{}; li.QuadPart = (LONGLONG)offset;
    SetFilePointerEx(m_handle, li, nullptr, FILE_BEGIN);

    DWORD written = 0;
    if (!WriteFile(m_handle, buf, count, &written, nullptr)) return -1;
    return (int64_t)written;
}

std::string PhysicalDiskSource::GetDescription() const { return "Physical: " + m_path; }
