// GEX - Graphical Explorer
#include "MftReader.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace gex::mft
{
    static inline uint16_t Rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    static inline uint32_t Rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
    static inline uint64_t Rd64(const uint8_t* p) { return (uint64_t)Rd32(p) | ((uint64_t)Rd32(p + 4) << 32); }
    static constexpr uint64_t kRefMask = 0x0000FFFFFFFFFFFFull;   // file reference: 48-bit record number + 16-bit sequence
    static constexpr uint64_t kRootRecord = 5;

    // NTFS names are UTF-16LE (unpaired surrogates are possible: replace them)
    static std::string Utf16ToUtf8(const uint8_t* p, size_t chars)
    {
        std::string out;
        out.reserve(chars);
        for (size_t i = 0; i < chars; ++i)
        {
            uint32_t c = Rd16(p + i * 2);
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < chars)
            {
                const uint32_t lo = Rd16(p + (i + 1) * 2);
                if (lo >= 0xDC00 && lo <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); ++i; }
                else c = 0xFFFD;
            }
            else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
            if (c < 0x80) out += (char)c;
            else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
            else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
            else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
        }
        return out;
    }

    bool ParseBootSector(const uint8_t* s, VolumeInfo& vi)
    {
        if (memcmp(s + 3, "NTFS    ", 8) != 0) return false;
        vi.bytesPerSector = Rd16(s + 0x0B);
        const uint32_t spc = s[0x0D];
        if (vi.bytesPerSector == 0 || spc == 0) return false;
        // sectors per cluster > 128 is stored as a negative shift on very large clusters
        vi.bytesPerCluster = spc <= 0x80 ? vi.bytesPerSector * spc : vi.bytesPerSector << (256 - spc);
        vi.mftStartLcn = Rd64(s + 0x30);
        const int8_t cpr = (int8_t)s[0x40];
        vi.recordSize = cpr > 0 ? (uint32_t)cpr * vi.bytesPerCluster : 1u << (uint32_t)(-cpr);
        vi.mftValidLength = 0;
        return vi.recordSize >= 256 && vi.recordSize <= 65536;
    }

    // Undo the "update sequence" protection: the last 2 bytes of every 512-byte stride were replaced
    // by a check value on disk; the originals are stored in the update sequence array.
    static bool ApplyFixup(uint8_t* rec, uint32_t recSize)
    {
        const uint16_t usaOff = Rd16(rec + 4), usaCount = Rd16(rec + 6);
        if (usaCount < 2 || (uint32_t)usaOff + usaCount * 2u > recSize) return false;
        const uint16_t check = Rd16(rec + usaOff);
        for (uint32_t i = 1; i < usaCount; ++i)
        {
            const uint32_t pos = i * 512u - 2u;
            if (pos + 2 > recSize) break;
            if (Rd16(rec + pos) != check) return false;   // torn write / corrupt record
            memcpy(rec + pos, rec + usaOff + i * 2, 2);
        }
        return true;
    }

    struct Run { int64_t lcn; uint64_t len; };   // lcn < 0: sparse

    static bool DecodeRuns(const uint8_t* p, const uint8_t* end, std::vector<Run>& runs)
    {
        int64_t lcn = 0;
        while (p < end && *p)
        {
            const uint8_t h = *p++;
            const int lb = h & 0x0F, ob = h >> 4;
            if (lb == 0 || lb > 8 || ob > 8 || p + lb + ob > end) return false;
            uint64_t len = 0;
            for (int i = 0; i < lb; ++i) len |= (uint64_t)p[i] << (8 * i);
            p += lb;
            if (ob == 0) { runs.push_back({ -1, len }); continue; }
            uint64_t raw = 0;
            for (int i = 0; i < ob; ++i) raw |= (uint64_t)p[i] << (8 * i);
            if (ob < 8 && (p[ob - 1] & 0x80)) raw |= ~0ull << (8 * ob);   // sign extend
            p += ob;
            lcn += (int64_t)raw;
            if (lcn < 0) return false;
            runs.push_back({ lcn, len });
        }
        return true;
    }

    struct Attr
    {
        uint32_t type, length;
        bool nonResident;
        uint8_t nameLength;
        const uint8_t* base;
    };

    // Iterate the attributes of a fixed-up record.
    template <typename F>
    static void ForEachAttr(const uint8_t* rec, uint32_t recSize, F&& f)
    {
        uint32_t off = Rd16(rec + 0x14);
        const uint32_t used = std::min<uint32_t>(Rd32(rec + 0x18), recSize);
        while (off + 16 <= used)
        {
            const uint8_t* a = rec + off;
            const uint32_t type = Rd32(a);
            if (type == 0xFFFFFFFF) break;
            const uint32_t len = Rd32(a + 4);
            if (len < 16 || off + len > used) break;
            if (!f(Attr{ type, len, a[8] != 0, a[9], a })) break;
            off += len;
        }
    }

    static bool ResidentValue(const Attr& at, const uint8_t*& v, uint32_t& vlen)
    {
        if (at.nonResident || at.length < 0x18) return false;
        vlen = Rd32(at.base + 0x10);
        const uint16_t voff = Rd16(at.base + 0x14);
        if ((uint32_t)voff + vlen > at.length) return false;
        v = at.base + voff;
        return true;
    }

    bool BuildTree(const VolumeInfo& vi, const ReadFn& read, FileTree& tree, const Counters& c, std::string& error)
    {
        const uint32_t rs = vi.recordSize, bpc = vi.bytesPerCluster;
        if (rs < 256 || bpc == 0 || (rs % 512) != 0) { error = "Unsupported NTFS geometry"; return false; }

        // ---- 1. $MFT record 0 -> where the MFT itself lives (its data runs)
        const uint32_t firstRead = ((std::max(rs, bpc) + bpc - 1) / bpc) * bpc;
        std::vector<uint8_t> first(firstRead);
        if (!read(vi.mftStartLcn * bpc, first.data(), firstRead)) { error = "Cannot read the MFT"; return false; }
        if (memcmp(first.data(), "FILE", 4) != 0 || !ApplyFixup(first.data(), rs)) { error = "MFT record 0 is invalid"; return false; }
        std::vector<Run> runs;
        uint64_t mftBytes = 0;
        bool haveData = false;
        ForEachAttr(first.data(), rs, [&](const Attr& at)
        {
            if (at.type == 0x80 && at.nameLength == 0 && at.nonResident && at.length >= 0x40)
            {
                if (Rd64(at.base + 0x10) != 0) return true;   // not the first extent
                mftBytes = Rd64(at.base + 0x38);               // valid data length
                if (mftBytes == 0) mftBytes = Rd64(at.base + 0x30);
                const uint16_t mp = Rd16(at.base + 0x20);
                haveData = mp < at.length && DecodeRuns(at.base + mp, at.base + at.length, runs);
                return false;
            }
            return true;
        });
        if (!haveData || runs.empty()) { error = "Cannot locate the MFT data"; return false; }
        if (vi.mftValidLength) mftBytes = std::min(mftBytes, vi.mftValidLength);
        uint64_t runBytes = 0;
        for (auto& r : runs) runBytes += r.len * bpc;
        if (runBytes < mftBytes)
        {
            // $MFT is so fragmented that its run list continues in an extension record (attribute list).
            error = "MFT is too fragmented for the fast scan";
            return false;
        }
        const uint64_t total = mftBytes / rs;
        if (total <= kRootRecord) { error = "MFT is too small"; return false; }

        // ---- 2. read every record
        std::vector<Node*>    nodes((size_t)total, nullptr);
        std::vector<uint64_t> parent((size_t)total, 0);
        std::vector<uint8_t>  nameRank((size_t)total, 255);   // 0 = Win32/POSIX name, 1 = DOS 8.3 name, 255 = none
        std::vector<uint8_t>  isBase((size_t)total, 0);
        NodeArena* arena = tree.NewArena();
        nodes[kRootRecord] = tree.root;

        auto nodeFor = [&](uint64_t frn) -> Node*
        {
            Node*& n = nodes[(size_t)frn];
            if (!n) n = arena->Alloc();
            return n;
        };

        const uint32_t chunk = std::max<uint32_t>(bpc, (8u << 20) / bpc * bpc);   // ~8 MB
        std::vector<uint8_t> buf(chunk);
        uint64_t frn = 0;
        for (const Run& run : runs)
        {
            uint64_t runOff = 0;
            const uint64_t runLen = run.len * bpc;
            while (runOff < runLen && frn < total)
            {
                if (c.cancel && c.cancel->load()) { error = "Cancelled"; return false; }
                const uint32_t n = (uint32_t)std::min<uint64_t>(chunk, runLen - runOff);
                const uint32_t recs = n / rs;
                if (run.lcn < 0) { frn += recs; runOff += n; continue; }   // sparse part of the MFT
                if (!read((uint64_t)run.lcn * bpc + runOff, buf.data(), n)) { error = "Read error in the MFT"; return false; }
                for (uint32_t i = 0; i < recs && frn < total; ++i, ++frn)
                {
                    uint8_t* rec = buf.data() + (size_t)i * rs;
                    if (memcmp(rec, "FILE", 4) != 0) continue;
                    const uint16_t flags = Rd16(rec + 0x16);
                    if (!(flags & 0x01)) continue;                                  // record not in use (deleted)
                    if (!ApplyFixup(rec, rs)) continue;
                    const uint64_t baseRef = Rd64(rec + 0x20) & kRefMask;
                    const uint64_t owner = baseRef ? baseRef : frn;                  // extension records belong to their base
                    if (owner >= total) continue;
                    Node* node = nodeFor(owner);
                    if (!baseRef)
                    {
                        isBase[(size_t)frn] = 1;
                        if (flags & 0x02) { node->flags |= NF_Dir; node->attrs |= 0x10; }   // FILE_ATTRIBUTE_DIRECTORY
                    }
                    ForEachAttr(rec, rs, [&](const Attr& at)
                    {
                        const uint8_t* v; uint32_t vlen;
                        switch (at.type)
                        {
                        case 0x10: // $STANDARD_INFORMATION
                            if (ResidentValue(at, v, vlen) && vlen >= 0x24)
                            {
                                node->mtime = Rd64(v + 0x08);
                                node->attrs = (node->attrs & 0x10) | (Rd32(v + 0x20) & ~0x10u);
                                if ((node->attrs & 0x400) && node->IsDir()) node->flags |= NF_Reparse;
                            }
                            break;
                        case 0x30: // $FILE_NAME
                            if (owner != kRootRecord && ResidentValue(at, v, vlen) && vlen >= 0x42)
                            {
                                const uint8_t len = v[0x40], ns = v[0x41];
                                if (0x42u + len * 2u > vlen) break;
                                const uint8_t rank = ns == 2 ? 1 : 0;              // prefer the long name over the 8.3 one
                                if (rank < nameRank[(size_t)owner])
                                {
                                    nameRank[(size_t)owner] = rank;
                                    parent[(size_t)owner] = Rd64(v) & kRefMask;
                                    node->name = Utf16ToUtf8(v + 0x42, len);
                                }
                            }
                            break;
                        case 0x80: // $DATA (unnamed stream = file contents)
                            if (at.nameLength == 0)
                            {
                                if (!at.nonResident) { if (ResidentValue(at, v, vlen)) node->size = vlen; }
                                else if (at.length >= 0x38 && Rd64(at.base + 0x10) == 0) node->size = Rd64(at.base + 0x30);
                            }
                            break;
                        }
                        return true;
                    });
                }
                runOff += n;
                if (c.fraction) c.fraction->store((float)((double)frn / (double)total));
            }
        }

        // ---- 3. link children to parents
        uint64_t files = 0, dirs = 0, bytes = 0;
        for (uint64_t i = 0; i < total; ++i)
        {
            Node* n = nodes[(size_t)i];
            if (!n || i == kRootRecord || !isBase[(size_t)i] || nameRank[(size_t)i] == 255) continue;
            const uint64_t p = parent[(size_t)i];
            if (p >= total || p == i || !nodes[(size_t)p] || !isBase[(size_t)p] || !nodes[(size_t)p]->IsDir()) continue;   // orphan
            Node* pn = nodes[(size_t)p];
            n->parent = pn;
            pn->children.push_back(n);
            if (n->IsDir()) { n->size = 0; ++dirs; }
            else { n->category = (uint8_t)ClassifyName(n->name); ++files; bytes += n->size; }
        }
        // Folders linked under an unreachable parent stay out of the tree (Finalize walks from the root).
        if (c.files) c.files->store(files);
        if (c.dirs) c.dirs->store(dirs);
        if (c.bytes) c.bytes->store(bytes);
        if (c.fraction) c.fraction->store(1.0f);
        return true;
    }
}
