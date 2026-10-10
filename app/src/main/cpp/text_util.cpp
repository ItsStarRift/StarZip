#include "text_util.h"

#include <cstdint>

UString ToUString(const std::u16string &s) {
    std::wstring wide;
    wide.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() &&
            s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(s[i + 1]) - 0xDC00);
            ++i;
        }
        wide.push_back(static_cast<wchar_t>(cp));
    }
    return UString(wide.c_str());
}
