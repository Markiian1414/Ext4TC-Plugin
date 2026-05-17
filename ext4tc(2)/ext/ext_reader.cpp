#include "ext_reader.h"
#define WIN32_LEAN_AND_MEAN
// Додаємо цей макрос, щоб Windows не ламав std::min
#define NOMINMAX 
#include <windows.h>
#include <functional>
#include <cstring>
#include <algorithm>
#include <sstream>

static std::vector<std::string> SplitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string token;
    while (std::getline(ss, token, '/'))
        if (!token.empty()) parts.push_back(token);
    return parts;
}

FILETIME UnixToFileTime(uint32_t unixTime) {
    LONGLONG ll = (LONGLONG)unixTime * 10000000LL + 116444736000000000LL;
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(ll & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(ll >> 32);
    return ft;
}

ExtReader::ExtReader() : m_partOffset(0), m_mounted(false) {}

bool ExtReader::Mount(std::shared_ptr<IDiskSource> src, uint64_t partitionOffset) {
    Unmount();
    m_src = src;
    m_partOffset = partitionOffset;

    if (!ReadSuperblock()) return false;

    m_info.block_size = 1024u << m_sb.s_log_block_size;
    m_info.inodes_per_group = m_sb.s_inodes_per_group;
    m_info.blocks_per_group = m_sb.s_blocks_per_group;
    m_info.inode_size = (m_sb.s_rev_level >= 1) ? m_sb.s_inode_size : 128;
    m_info.desc_size = (m_sb.s_desc_size >= 64) ? 64 : 32;
    m_info.has_extents = !!(m_sb.s_feature_incompat & EXT4_FEATURE_INCOMPAT_EXTENTS);
    m_info.is_64bit = !!(m_sb.s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT);
    m_info.has_journal = !!(m_sb.s_feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL);

    uint64_t totalBlocks = m_sb.s_blocks_count_lo;
    if (m_info.is_64bit) totalBlocks |= ((uint64_t)m_sb.s_blocks_count_hi << 32);
    m_info.group_count = (uint32_t)((totalBlocks + m_sb.s_blocks_per_group - 1)
        / m_sb.s_blocks_per_group);

    memcpy(m_info.volume_name, m_sb.s_volume_name, 16);
    m_info.volume_name[16] = '\0';

    m_mounted = true;
    return true;
}

void ExtReader::Unmount() {
    m_mounted = false;
    m_src = nullptr;
    m_partOffset = 0;
}

bool ExtReader::ReadSuperblock() {
    return m_src->ReadExact(m_partOffset + EXT2_SUPERBLOCK_OFFSET,
        &m_sb, 1024)
        && m_sb.s_magic == EXT2_SUPER_MAGIC;
}

uint64_t ExtReader::BlockOffset(uint64_t blockNum) const {
    return m_partOffset + blockNum * m_info.block_size;
}

bool ExtReader::ReadBlock(uint64_t blockNum, void* buf, uint32_t size) {
    uint32_t readSize = size ? size : m_info.block_size;
    return m_src->ReadExact(BlockOffset(blockNum), buf, readSize);
}

uint64_t ExtReader::InodeOffset(uint32_t inodeNum) const {
    uint32_t idx = inodeNum - 1;
    uint32_t group = idx / m_info.inodes_per_group;
    uint32_t local = idx % m_info.inodes_per_group;

    uint64_t gdtBlock = (m_info.block_size == 1024) ? 2 : 1;
    uint64_t gdtOffset = BlockOffset(gdtBlock) + (uint64_t)group * m_info.desc_size;

    ext2_group_desc gd{};
    m_src->ReadExact(gdtOffset, &gd, sizeof(gd));

    uint64_t inodeTable = gd.bg_inode_table_lo;
    if (m_info.is_64bit)
        inodeTable |= ((uint64_t)gd.bg_inode_table_hi << 32);

    return BlockOffset(inodeTable) + (uint64_t)local * m_info.inode_size;
}

bool ExtReader::ReadInode(uint32_t inodeNum, ext2_inode& inode) {
    if (inodeNum == 0) return false;
    uint64_t off = InodeOffset(inodeNum);
    memset(&inode, 0, sizeof(inode));
    uint32_t readSize = (std::min)((uint32_t)sizeof(inode), (uint32_t)m_info.inode_size);
    return m_src->ReadExact(off, &inode, readSize);
}

bool ExtReader::CollectExtents(const ext2_inode& inode,
    std::vector<std::pair<uint64_t, uint64_t>>& out) {
    const uint8_t* root = (const uint8_t*)inode.i_block;
    const auto* hdr = (const ext4_extent_header*)root;

    if (hdr->eh_magic != EXT4_EXTENT_MAGIC) return false;

    std::function<bool(const uint8_t*, int)> traverse =
        [&](const uint8_t* node, int depth) -> bool {
        const auto* h = (const ext4_extent_header*)node;
        // Захист від пошкодженого заголовку: надто багато записів.
        // Максимум залежить від розміру блоку, а не фіксований (340 було для 4096).
        const uint32_t maxEntries = (m_info.block_size - (uint32_t)sizeof(ext4_extent_header))
            / (uint32_t)sizeof(ext4_extent);
        if (h->eh_entries > maxEntries) return false;
        if (h->eh_depth == 0) {
            const auto* ext = (const ext4_extent*)(node + sizeof(ext4_extent_header));
            for (int i = 0; i < h->eh_entries; i++) {
                uint64_t physBlock = ext[i].ee_start_lo
                    | ((uint64_t)ext[i].ee_start_hi << 32);
                for (uint32_t b = 0; b < ext[i].ee_len; b++)
                    out.push_back({ ext[i].ee_block + b, physBlock + b });
            }
        }
        else {
            const auto* idx = (const ext4_extent_idx*)(node + sizeof(ext4_extent_header));
            for (int i = 0; i < h->eh_entries; i++) {
                uint64_t childBlock = idx[i].ei_leaf_lo
                    | ((uint64_t)idx[i].ei_leaf_hi << 32);
                std::vector<uint8_t> childBuf(m_info.block_size);
                if (!ReadBlock(childBlock, childBuf.data()))
                    return false;
                if (!traverse(childBuf.data(), depth - 1))
                    return false;
            }
        }
        return true;
        };

    return traverse(root, hdr->eh_depth);
}

bool ExtReader::CollectIndirect(uint32_t blockNum, int depth,
    std::vector<uint64_t>& blocks) {
    if (blockNum == 0) return true;
    std::vector<uint32_t> buf(m_info.block_size / 4);
    if (!ReadBlock(blockNum, buf.data())) return false;

    for (uint32_t i = 0; i < m_info.block_size / 4; i++) {
        if (buf[i] == 0) continue;
        if (depth == 0)
            blocks.push_back(buf[i]);
        else
            CollectIndirect(buf[i], depth - 1, blocks);
    }
    return true;
}

bool ExtReader::GetFileBlocks(const ext2_inode& inode,
    std::vector<std::pair<uint64_t, uint64_t>>& extents) {
    extents.clear();
    if (inode.i_flags & EXT4_EXTENTS_FL) {
        return CollectExtents(inode, extents);
    }

    std::vector<uint64_t> blocks;
    for (int i = 0; i < 12; i++)
        if (inode.i_block[i]) blocks.push_back(inode.i_block[i]);
    CollectIndirect(inode.i_block[12], 0, blocks);
    CollectIndirect(inode.i_block[13], 1, blocks);
    CollectIndirect(inode.i_block[14], 2, blocks);

    for (uint64_t i = 0; i < blocks.size(); i++)
        extents.push_back({ i, blocks[i] });
    return true;
}

bool ExtReader::ReadFileData(const ext2_inode& inode, uint32_t inodeNum,
    std::vector<uint8_t>& out) {
    uint64_t fileSize = inode.i_size_lo | ((uint64_t)inode.i_size_hi << 32);
    if (fileSize == 0) { out.clear(); return true; }

    if (EXT2_S_ISLNK(inode.i_mode) && fileSize <= 60) {
        out.assign((uint8_t*)inode.i_block,
            (uint8_t*)inode.i_block + fileSize);
        return true;
    }

    std::vector<std::pair<uint64_t, uint64_t>> extents;
    if (!GetFileBlocks(inode, extents)) return false;

    // Якщо файл має розмір, але жодного блоку не знайдено — пошкоджена ФС
    if (extents.empty() && fileSize > 0) return false;

    // Сортуємо за логічним номером блоку: extent-дерево може повертати
    // блоки не в порядку (наприклад після дефрагментації), а запис у out
    // йде послідовно — без сортування дані будуть перемішані.
    std::sort(extents.begin(), extents.end(),
        [](const std::pair<uint64_t, uint64_t>& a,
            const std::pair<uint64_t, uint64_t>& b) { return a.first < b.first; });

    out.resize((size_t)fileSize);
    uint64_t written = 0;

    for (auto& [logBlock, physBlock] : extents) {
        if (written >= fileSize) break;
        uint64_t blockOff = BlockOffset(physBlock);
        uint32_t readSize = (uint32_t)(std::min)((uint64_t)m_info.block_size,
            fileSize - written);
        if (!m_src->ReadExact(blockOff, out.data() + written, readSize))
            return false;
        written += readSize;
    }
    out.resize((size_t)fileSize);
    return true;
}

bool ExtReader::ParseDirectory(const ext2_inode& dirInode, uint32_t inodeNum,
    std::vector<ExtEntry>& out) {
    std::vector<uint8_t> data;
    if (!ReadFileData(dirInode, inodeNum, data)) return false;

    size_t pos = 0;
    while (pos + 8 <= data.size()) {
        const auto* de = (const ext2_dir_entry*)(data.data() + pos);
        // Захист від пошкодженого запису: rec_len < 8 або не вирівняний
        // призведе до нескінченного циклу або виходу за межі буфера
        if (de->rec_len < 8) break;

        if (de->inode != 0 && de->name_len > 0) {
            std::string name(de->name, de->name_len);
            if (name != "." && name != "..") {
                ext2_inode childInode{};
                if (ReadInode(de->inode, childInode)) {
                    ExtEntry e;
                    e.name = name;
                    e.inode_num = de->inode;
                    e.is_dir = EXT2_S_ISDIR(childInode.i_mode);
                    e.is_symlink = EXT2_S_ISLNK(childInode.i_mode);
                    e.size = childInode.i_size_lo
                        | ((uint64_t)childInode.i_size_hi << 32);
                    // ВИПРАВЛЕНО доступ до osd2
                    e.uid = childInode.i_uid
                        | ((uint32_t)childInode.osd2.l_i_uid_hi << 16);
                    e.gid = childInode.i_gid
                        | ((uint32_t)childInode.osd2.l_i_gid_hi << 16);
                    e.mode = childInode.i_mode;
                    e.atime = childInode.i_atime;
                    e.mtime = childInode.i_mtime;
                    e.ctime = childInode.i_ctime;
                    if (e.is_symlink)
                        e.link_target = ReadSymlink(childInode, de->inode);
                    out.push_back(e);
                }
            }
        }
        pos += de->rec_len;
    }
    return true;
}

std::string ExtReader::ReadSymlink(const ext2_inode& inode, uint32_t inodeNum) {
    uint64_t len = inode.i_size_lo;
    if (len == 0 || len > 4096) return "";
    if (len <= 60) {
        return std::string((const char*)inode.i_block, (size_t)len);
    }
    std::vector<uint8_t> data;
    if (!ReadFileData(inode, inodeNum, data)) return "";
    return std::string((char*)data.data(), data.size());
}

bool ExtReader::LookupInDir(const ext2_inode& dirInode, uint32_t dirInum,
    const std::string& name, uint32_t& foundInum) {
    std::vector<uint8_t> data;
    if (!ReadFileData(dirInode, dirInum, data)) return false;

    size_t pos = 0;
    while (pos + 8 <= data.size()) {
        const auto* de = (const ext2_dir_entry*)(data.data() + pos);
        if (de->rec_len < 8) break;
        if (de->inode != 0 && de->name_len == name.size() &&
            memcmp(de->name, name.c_str(), de->name_len) == 0) {
            foundInum = de->inode;
            return true;
        }
        pos += de->rec_len;
    }
    return false;
}

bool ExtReader::GetInodeByPath(const std::string& path,
    ext2_inode& inode, uint32_t& inode_num, int symlinkDepth) {
    // Захист від циклічних символічних посилань: максимальна глибина = 40
    // (відповідає поведінці ядра Linux — MAXSYMLINKS)
    if (symlinkDepth > 40) return false;

    auto parts = SplitPath(path);
    uint32_t curInum = EXT2_ROOT_INO;

    if (!ReadInode(curInum, inode)) return false;
    if (parts.empty()) { inode_num = curInum; return true; }

    for (auto& part : parts) {
        if (!EXT2_S_ISDIR(inode.i_mode)) return false;
        uint32_t nextInum = 0;
        if (!LookupInDir(inode, curInum, part, nextInum)) return false;
        if (!ReadInode(nextInum, inode)) return false;
        curInum = nextInum;
        if (EXT2_S_ISLNK(inode.i_mode)) {
            std::string target = ReadSymlink(inode, curInum);
            if (!target.empty()) {
                uint32_t linkInum = 0;
                ext2_inode linkInode{};
                if (GetInodeByPath(target, linkInode, linkInum, symlinkDepth + 1)) {
                    inode = linkInode;
                    curInum = linkInum;
                }
            }
        }
    }
    inode_num = curInum;
    return true;
}

bool ExtReader::ListDirectory(const std::string& path, std::vector<ExtEntry>& out) {
    out.clear();
    ext2_inode inode{};
    uint32_t   inum = 0;
    if (!GetInodeByPath(path, inode, inum)) return false;
    if (!EXT2_S_ISDIR(inode.i_mode)) return false;
    return ParseDirectory(inode, inum, out);
}

bool ExtReader::ExtractFile(const std::string& extPath,
    const std::string& localPath,
    std::function<bool(uint64_t, uint64_t)> progress) {
    ext2_inode inode{};
    uint32_t   inum = 0;
    if (!GetInodeByPath(extPath, inode, inum)) return false;
    if (!EXT2_S_ISREG(inode.i_mode)) return false;

    uint64_t fileSize = inode.i_size_lo | ((uint64_t)inode.i_size_hi << 32);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, localPath.c_str(), -1, nullptr, 0);
    std::wstring wp(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, localPath.c_str(), -1, wp.data(), wlen);

    HANDLE hOut = CreateFileW(wp.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) return false;

    std::vector<std::pair<uint64_t, uint64_t>> extents;
    if (!GetFileBlocks(inode, extents)) { CloseHandle(hOut); return false; }

    // Сортуємо за логічним номером блоку (аналогічно ReadFileData)
    std::sort(extents.begin(), extents.end(),
        [](const std::pair<uint64_t, uint64_t>& a,
            const std::pair<uint64_t, uint64_t>& b) { return a.first < b.first; });

    std::vector<uint8_t> blockBuf(m_info.block_size);
    uint64_t written = 0;
    bool     ok = true;

    for (auto& [logBlock, physBlock] : extents) {
        if (written >= fileSize) break;
        uint32_t readSize = (uint32_t)(std::min)((uint64_t)m_info.block_size,
            fileSize - written);
        if (!m_src->ReadExact(BlockOffset(physBlock), blockBuf.data(), readSize)) {
            ok = false; break;
        }
        DWORD dw = 0;
        // ВИПРАВЛЕНО: додано :: перед WriteFile
        ::WriteFile(hOut, blockBuf.data(), readSize, &dw, nullptr);
        written += readSize;

        if (progress && !progress(written, fileSize)) {
            ok = false; break;
        }
    }

    CloseHandle(hOut);

    FILETIME ft = UnixToFileTime(inode.i_mtime);
    HANDLE hRO = CreateFileW(wp.c_str(), FILE_WRITE_ATTRIBUTES, 0,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (hRO != INVALID_HANDLE_VALUE) {
        SetFileTime(hRO, nullptr, nullptr, &ft);
        CloseHandle(hRO);
    }
    return ok;
}

bool ExtReader::WriteFile(const std::string& localPath, const std::string& extPath) {
    if (!m_src || m_src->IsReadOnly()) return false;
    return false;
}

bool ExtReader::MakeDir(const std::string& path) {
    if (!m_src || m_src->IsReadOnly()) return false;
    return false;
}

bool ExtReader::RemoveFile(const std::string& path) {
    if (!m_src || m_src->IsReadOnly()) return false;
    return false;
}