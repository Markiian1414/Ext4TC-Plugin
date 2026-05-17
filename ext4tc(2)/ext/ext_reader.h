#pragma once
#ifndef EXT_READER_H
#define EXT_READER_H

#include <string>
#include <vector>
#include <memory>
#include <ctime>
#include <functional> // <--- критично для компіляції

#include "../io/disk_source.h"
#include "ext_types.h"

struct ExtEntry {
    std::string  name;
    uint32_t     inode_num;
    bool         is_dir;
    bool         is_symlink;
    uint64_t     size;
    uint32_t     uid;
    uint32_t     gid;
    uint16_t     mode;
    time_t       atime;
    time_t       mtime;
    time_t       ctime;
    std::string  link_target;
};

class ExtReader {
public:
    ExtReader();
    ~ExtReader() = default;

    bool Mount(std::shared_ptr<IDiskSource> src, uint64_t partitionOffset = 0);
    void Unmount();
    bool IsMounted() const { return m_mounted; }

    const ExtFsInfo& GetFsInfo() const { return m_info; }

    bool ListDirectory(const std::string& path, std::vector<ExtEntry>& out);

    bool ExtractFile(const std::string& extPath, const std::string& localPath,
        std::function<bool(uint64_t done, uint64_t total)> progress = nullptr);

    bool WriteFile(const std::string& localPath, const std::string& extPath);
    bool MakeDir(const std::string& path);

    // Перейменовано з DeleteFile (яке конфліктувало з WinAPI)
    bool RemoveFile(const std::string& path);

    bool GetInodeByPath(const std::string& path, ext2_inode& inode, uint32_t& inode_num,
        int symlinkDepth = 0);
    std::string ReadSymlink(const ext2_inode& inode, uint32_t inode_num);

private:
    std::shared_ptr<IDiskSource> m_src;
    uint64_t  m_partOffset;
    bool      m_mounted;
    ExtFsInfo m_info;
    ext2_super_block m_sb;

    bool ReadSuperblock();
    bool ReadBlock(uint64_t blockNum, void* buf, uint32_t size = 0);
    bool ReadInode(uint32_t inodeNum, ext2_inode& inode);

    uint64_t BlockOffset(uint64_t blockNum) const;
    uint64_t InodeOffset(uint32_t inodeNum) const;

    bool CollectExtents(const ext2_inode& inode, std::vector<std::pair<uint64_t, uint64_t>>& logToPhys);
    bool CollectIndirect(uint32_t blockNum, int depth, std::vector<uint64_t>& blocks);
    bool GetFileBlocks(const ext2_inode& inode, std::vector<std::pair<uint64_t, uint64_t>>& extents);
    bool ReadFileData(const ext2_inode& inode, uint32_t inodeNum, std::vector<uint8_t>& out);
    bool ParseDirectory(const ext2_inode& dirInode, uint32_t inodeNum, std::vector<ExtEntry>& out);
    bool LookupInDir(const ext2_inode& dirInode, uint32_t dirInum, const std::string& name, uint32_t& foundInum);
};

#endif // EXT_READER_H