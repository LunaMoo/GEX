// GEX - Graphical Explorer
// File categories ("general media groups") and their colors.
#pragma once
#include <cstdint>
#include <string>

namespace gex
{
    enum Category : uint8_t
    {
        Cat_Video = 0,
        Cat_Audio,
        Cat_Image,
        Cat_Archive,
        Cat_Document,   // text, office, pdf, ebooks
        Cat_Code,
        Cat_Program,    // exe, dll, msi, sys ...
        Cat_DiscImage,  // iso, cso, chd, rvz, vhd ...
        Cat_TempLog,    // tmp, log, dmp, bak, cache ...
        Cat_Other,
        Cat_COUNT,
        Cat_Folder = 254 // not a real category, used for coloring folders
    };

    struct CategoryInfo
    {
        const char* name;
        const char* description;
        float color[3];      // editable at runtime (File Types tab), saved to settings
    };

    CategoryInfo& GetCategoryInfo(int cat);
    const char*   CategoryName(int cat);
    uint32_t      CategoryColorU32(int cat, float alpha = 1.0f);   // ImU32 (ABGR)
    uint32_t      FolderColorU32(float alpha = 1.0f);
    float*        FolderColor();

    // Classify by lowercase extension (no dot)
    Category ClassifyExtension(const std::string& extLower);
    // Classify by file name (UTF-8)
    Category ClassifyName(const std::string& nameUtf8);

    void ResetCategoryColors();
}
