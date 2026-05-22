#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "image_source.h"

ImageFileSource::ImageFileSource()
    : m_handle(INVALID_HANDLE_VALUE), m_readOnly(true), m_sizeBytes(0) {}

ImageFileSource::~ImageFileSource() { Close(); }

bool ImageFileSource::Open(const std::string& path, bool readOnly) {
    Close();
    m_readOnly = readOnly;
    m_path = path;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wp(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wp.data(), wlen);

    DWORD access = readOnly ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE;
    DWORD share  = FILE_SHARE_READ;

    m_handle = CreateFileW(wp.c_str(), access, share, nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                           nullptr);
    if (m_handle == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(m_handle, &sz)) { Close(); return false; }
    m_sizeBytes = (uint64_t)sz.QuadPart;
    return true;
}

void ImageFileSource::Close() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
    m_sizeBytes = 0;
}

bool     ImageFileSource::IsOpen()     const { return m_handle != INVALID_HANDLE_VALUE; }
bool     ImageFileSource::IsReadOnly() const { return m_readOnly; }
uint64_t ImageFileSource::GetSizeBytes() const { return m_sizeBytes; }

int64_t ImageFileSource::ReadBytes(uint64_t offset, void* buf, uint32_t count) {
    if (!IsOpen() || !buf || !count) return -1;
    if (offset + count > m_sizeBytes) return -1;

    LARGE_INTEGER li{}; li.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(m_handle, li, nullptr, FILE_BEGIN)) return -1;

    DWORD got = 0;
    if (!ReadFile(m_handle, buf, count, &got, nullptr)) return -1;
    return (int64_t)got;
}

int64_t ImageFileSource::WriteBytes(uint64_t offset, const void* buf, uint32_t count) {
    if (m_readOnly || !IsOpen() || !buf || !count) return -1;

    LARGE_INTEGER li{}; li.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(m_handle, li, nullptr, FILE_BEGIN)) return -1;

    DWORD written = 0;
    if (!WriteFile(m_handle, buf, count, &written, nullptr)) return -1;
    return (int64_t)written;
}

std::string ImageFileSource::GetDescription() const { return "Image: " + m_path; }
