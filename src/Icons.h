// GEX - Graphical Explorer
// Glyphs from the Windows icon font (Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets on Windows 10),
// merged into the UI font at startup. Use gex::Ico() which returns "" if the font is missing.
#pragma once

#define ICON_BACK           "\xEE\x9C\xAB"   // U+E72B
#define ICON_FORWARD        "\xEE\x9C\xAA"   // U+E72A
#define ICON_UP             "\xEE\x9D\x8A"   // U+E74A
#define ICON_REFRESH        "\xEE\x9C\xAC"   // U+E72C
#define ICON_DELETE         "\xEE\x9D\x8D"   // U+E74D
#define ICON_PLAY           "\xEE\x9D\xA8"   // U+E768
#define ICON_PAUSE          "\xEE\x9D\xA9"   // U+E769
#define ICON_STOP           "\xEE\x9C\x9A"   // U+E71A
#define ICON_FOLDER         "\xEE\xA2\xB7"   // U+E8B7
#define ICON_FOLDER_OPEN    "\xEE\xA0\xB8"   // U+E838
#define ICON_SEARCH         "\xEE\x9C\xA1"   // U+E721
#define ICON_COPY           "\xEE\xA3\x88"   // U+E8C8
#define ICON_CUT            "\xEE\xA3\x86"   // U+E8C6
#define ICON_PASTE          "\xEE\x9D\xBF"   // U+E77F
#define ICON_RENAME         "\xEE\xA2\xAC"   // U+E8AC
#define ICON_SETTINGS       "\xEE\x9C\x93"   // U+E713
#define ICON_DRIVE          "\xEE\xB6\xA2"   // U+EDA2
#define ICON_HISTORY        "\xEE\xA0\x9C"   // U+E81C
#define ICON_DOCUMENT       "\xEE\xA2\xA5"   // U+E8A5
#define ICON_OPEN_EXTERNAL  "\xEE\xA2\xA7"   // U+E8A7
#define ICON_MORE           "\xEE\x9C\x92"   // U+E712
#define ICON_TILES          "\xEE\xB2\xA5"   // U+ECA5
#define ICON_PIE            "\xEE\xAC\x85"   // U+EB05
#define ICON_LIST           "\xEE\xA8\xB7"   // U+EA37
#define ICON_SHIELD         "\xEE\xA8\x98"   // U+EA18
#define ICON_INFO           "\xEE\xA5\x86"   // U+E946
#define ICON_FILTER         "\xEE\x9C\x9C"   // U+E71C
#define ICON_CLEAR          "\xEE\xA2\x94"   // U+E894
#define ICON_HOME           "\xEE\xA0\x8F"   // U+E80F
#define ICON_OPEN_WITH      "\xEE\x9E\xAC"   // U+E7AC
#define ICON_TERMINAL       "\xEE\x9D\x96"   // U+E756
#define ICON_NEW_FOLDER     "\xEE\xA3\xB4"   // U+E8F4
#define ICON_LINK           "\xEE\x9C\x9B"   // U+E71B
#define ICON_CHEVRON        "\xEE\x9D\xAC"   // U+E76C
#define ICON_ZOOM_IN        "\xEE\xA2\xA3"   // U+E8A3
#define ICON_ZOOM_OUT       "\xEE\x9C\x9F"   // U+E71F
#define ICON_EXPORT         "\xEE\xB7\xA1"   // U+EDE1
#define ICON_WARNING        "\xEE\x9E\xBA"   // U+E7BA
#define ICON_EYE            "\xEE\xA2\x90"   // U+E890
#define ICON_PULSE          "\xEE\xA7\x99"   // U+E9D9
#define ICON_SAVE           "\xEE\x9D\x8E"   // U+E74E
#define ICON_CHECK          "\xEE\x9C\xBE"   // U+E73E

#define ICON_RANGE_MIN 0xE700
#define ICON_RANGE_MAX 0xF8FF

namespace gex { extern bool g_haveIconFont; inline const char* Ico(const char* glyph) { return g_haveIconFont ? glyph : ""; } }
