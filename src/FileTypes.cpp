// GEX - Graphical Explorer
#include "FileTypes.h"
#include "Util.h"
#include <unordered_map>
#include <mutex>

namespace gex
{
    static CategoryInfo s_categories[Cat_COUNT] =
    {
        { "Videos",        "Movies, clips and recordings",              { 0.93f, 0.33f, 0.31f } },
        { "Music & Audio", "Songs, sound effects, podcasts",            { 0.67f, 0.40f, 0.93f } },
        { "Images",        "Photos, pictures, textures",                { 0.20f, 0.80f, 0.45f } },
        { "Archives",      "Compressed archives and packages",          { 0.98f, 0.85f, 0.20f } },
        { "Documents",     "Text, office documents, PDFs, e-books",     { 0.25f, 0.60f, 0.98f } },
        { "Source Code",   "Programming and markup files",              { 0.10f, 0.80f, 0.80f } },
        { "Programs",      "Executables, libraries, installers",        { 1.00f, 0.50f, 0.25f } },
        { "Disc Images",   "ISO/CSO/CHD/RVZ, virtual disks, ROMs",      { 0.95f, 0.35f, 0.65f } },
        { "Temp & Logs",   "Temporary files, logs, dumps, backups",     { 0.62f, 0.66f, 0.70f } },
        { "Other",         "Everything else",                           { 0.45f, 0.50f, 0.62f } },
    };
    static const float s_defaultColors[Cat_COUNT][3] =
    {
        { 0.93f, 0.33f, 0.31f }, { 0.67f, 0.40f, 0.93f }, { 0.20f, 0.80f, 0.45f }, { 0.98f, 0.85f, 0.20f },
        { 0.25f, 0.60f, 0.98f }, { 0.10f, 0.80f, 0.80f }, { 1.00f, 0.50f, 0.25f }, { 0.95f, 0.35f, 0.65f },
        { 0.62f, 0.66f, 0.70f }, { 0.45f, 0.50f, 0.62f },
    };
    static float s_folderColor[3] = { 0.96f, 0.72f, 0.30f };

    void ResetCategoryColors()
    {
        for (int i = 0; i < Cat_COUNT; ++i)
            for (int c = 0; c < 3; ++c) s_categories[i].color[c] = s_defaultColors[i][c];
        s_folderColor[0] = 0.96f; s_folderColor[1] = 0.72f; s_folderColor[2] = 0.30f;
    }

    CategoryInfo& GetCategoryInfo(int cat)
    {
        if (cat < 0 || cat >= Cat_COUNT) cat = Cat_Other;
        return s_categories[cat];
    }

    const char* CategoryName(int cat)
    {
        if (cat == Cat_Folder) return "Folder";
        return GetCategoryInfo(cat).name;
    }

    static uint32_t PackColor(const float* c, float a)
    {
        auto to8 = [](float f) -> uint32_t { if (f < 0) f = 0; if (f > 1) f = 1; return (uint32_t)(f * 255.0f + 0.5f); };
        return (to8(a) << 24) | (to8(c[2]) << 16) | (to8(c[1]) << 8) | to8(c[0]);
    }

    uint32_t CategoryColorU32(int cat, float alpha)
    {
        if (cat == Cat_Folder) return PackColor(s_folderColor, alpha);
        return PackColor(GetCategoryInfo(cat).color, alpha);
    }

    uint32_t FolderColorU32(float alpha) { return PackColor(s_folderColor, alpha); }
    float*   FolderColor() { return s_folderColor; }

    static const std::unordered_map<std::string, Category>& ExtMap()
    {
        static std::unordered_map<std::string, Category> map;
        static std::once_flag once;
        std::call_once(once, []()
        {
            auto add = [](Category c, std::initializer_list<const char*> exts) { for (auto e : exts) map[e] = c; };
            add(Cat_Video, { "mp4","mkv","avi","mov","wmv","flv","webm","m4v","mpg","mpeg","m2ts","mts","ts","vob","3gp","ogv","rm","rmvb","divx","f4v","asf","mxf","y4m","264","h264","265","hevc" });
            add(Cat_Audio, { "mp3","flac","wav","ogg","opus","m4a","aac","wma","alac","aiff","aif","ape","mid","midi","mka","ac3","dts","wv","at3","at9","vag","xm","mod","s3m","it","amr","caf" });
            add(Cat_Image, { "jpg","jpeg","png","gif","bmp","tif","tiff","webp","heic","heif","avif","raw","cr2","cr3","nef","arw","dng","orf","rw2","psd","xcf","kra","svg","ico","tga","dds","exr","hdr","jxl","jfif","pcx","cur","ani" });
            add(Cat_Archive, { "zip","rar","7z","tar","gz","tgz","bz2","tbz2","xz","txz","zst","lz","lz4","lzma","cab","arj","z","zipx","jar","apk","xapk","aab","ipa","deb","rpm","pkg","appx","msix","appxbundle","msixbundle","nupkg","whl","war","ear","pak","vpk","pk3","pk4","zim","sit","sitx" });
            add(Cat_Document, { "txt","md","rtf","pdf","doc","docx","odt","xls","xlsx","xlsm","ods","csv","tsv","ppt","pptx","odp","epub","mobi","azw","azw3","djvu","xps","oxps","one","pages","numbers","key","tex","rst","nfo","srt","ass","ssa","sub","vtt","chm","wpd","msg","eml","vcf","ics" });
            add(Cat_Code, { "c","cc","cpp","cxx","h","hh","hpp","hxx","inl","cs","java","kt","kts","py","pyw","js","mjs","cjs","tsx","jsx","go","rs","swift","m","mm","rb","php","pl","lua","sh","bash","ps1","psm1","bat","cmd","vbs","sql","html","htm","css","scss","less","xml","json","yaml","yml","toml","ini","cfg","conf","cmake","gradle","sln","vcxproj","filters","props","csproj","asm","s","glsl","hlsl","vert","frag","shader","ipynb","dart","scala","r","jl","zig","nim","v","sv","vhd","vhdl","gd","mk","makefile","patch","diff" });
            add(Cat_Program, { "exe","dll","msi","msp","sys","drv","ocx","com","scr","cpl","efi","so","dylib","appimage","lib","a","o","obj","pdb","ilk","exp","pyd","pyc","node","mui","winmd","elf","prx","sprx","self" });
            add(Cat_DiscImage, { "iso","cso","zso","dax","jso","chd","rvz","wia","gcz","wbfs","ciso","nkit","cue","bin","img","mdf","mds","nrg","ccd","gdi","cdi","pbp","vhd","vhdx","vmdk","vdi","qcow2","dmg","wim","esd","swm","nsp","xci","nsz","xcz","3ds","cia","cci","nds","gba","gb","gbc","nes","sfc","smc","n64","z64","v64","gcm","xiso" });
            add(Cat_TempLog, { "tmp","temp","log","dmp","mdmp","hdmp","bak","old","cache","etl","evtx","crdownload","part","partial","download","swp","swo","chk","gid","thumbs","blf","regtrans-ms","trace","journal","lock","wer" });
        });
        return map;
    }

    Category ClassifyExtension(const std::string& extLower)
    {
        if (extLower.empty()) return Cat_Other;
        const auto& map = ExtMap();
        auto it = map.find(extLower);
        return it == map.end() ? Cat_Other : it->second;
    }

    Category ClassifyName(const std::string& nameUtf8)
    {
        Category c = ClassifyExtension(ExtensionOf(nameUtf8));
        if (c == Cat_Other)
        {
            // A few well known extension-less / special names
            const std::string lower = ToLowerAscii(nameUtf8);
            if (lower == "pagefile.sys" || lower == "hiberfil.sys" || lower == "swapfile.sys") return Cat_TempLog;
            if (lower == "makefile" || lower == "dockerfile" || lower == "cmakelists.txt") return Cat_Code;
        }
        return c;
    }
}
