// GEX - Graphical Explorer
// NTFS master file table (MFT) reader: builds the whole file tree of a volume by reading the
// MFT sequentially, like WizTree. Much faster than enumerating directories, but needs
// administrator rights to open the raw volume. Platform independent: the caller supplies a
// function that reads raw bytes from the volume.
#pragma once
#include "FileTree.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace gex::mft
{
    struct VolumeInfo
    {
        uint32_t bytesPerSector = 512;
        uint32_t bytesPerCluster = 4096;
        uint32_t recordSize = 1024;      // bytes per file record segment
        uint64_t mftStartLcn = 0;        // cluster of $MFT record 0
        uint64_t mftValidLength = 0;     // 0 = take it from $MFT's own $DATA attribute
    };

    // Reads 'size' bytes at byte 'offset' of the volume. Offsets/sizes requested are multiples of bytesPerCluster.
    using ReadFn = std::function<bool(uint64_t offset, void* buffer, uint32_t size)>;

    struct Counters
    {
        std::atomic<uint64_t>* files = nullptr;
        std::atomic<uint64_t>* dirs = nullptr;
        std::atomic<uint64_t>* bytes = nullptr;
        std::atomic<float>*    fraction = nullptr;   // 0..1 progress through the MFT
        const std::atomic<bool>* cancel = nullptr;
    };

    // Parses the NTFS boot sector (first 512 bytes of the volume). Used by tests and as a fallback.
    bool ParseBootSector(const uint8_t* sector, VolumeInfo& out);

    // Fills 'tree' (whose root must represent the volume root, e.g. "C:\") with every file and folder.
    // Hidden and system files are included, NTFS metadata files ($MFT, $LogFile...) appear in the root.
    // Hard links are counted once (first name). Returns false with 'error' set on failure.
    bool BuildTree(const VolumeInfo& vi, const ReadFn& read, FileTree& tree, const Counters& c, std::string& error);
}
