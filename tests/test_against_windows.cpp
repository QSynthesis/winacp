// Compares every entry of every table with the Windows API from which it was generated.
//
// Windows only. A failure indicates either that the tables were modified manually or that the
// Windows conversion has changed since generation; in either case tools/generate must be run
// again. Comparing whole strings also verifies an assumption of the table format: that Windows
// decodes each sequence independently of the preceding bytes.
#include <cstdio>
#include <optional>
#include <string>

#include <windows.h>

#include <winacp/winacp.h>

#include "check.h"

namespace {

    /// Decodes \a bytes with the Windows API, returning std::nullopt if Windows rejects them.
    std::optional<std::u16string> windowsDecode(UINT page, const std::string &bytes) {
        if (bytes.empty()) {
            return std::u16string();
        }
        wchar_t text[8];
        const int length = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, bytes.data(),
                                               int(bytes.size()), text, 8);
        if (length <= 0) {
            return std::nullopt;
        }
        return std::u16string(reinterpret_cast<const char16_t *>(text), size_t(length));
    }

    /// Encodes \a text with the Windows API. \a substituted is set if the default character
    /// was used.
    std::string windowsEncode(UINT page, const std::u16string &text, bool *substituted) {
        char bytes[32];
        BOOL defaulted = FALSE;
        const int size = WideCharToMultiByte(
            page, WC_NO_BEST_FIT_CHARS, reinterpret_cast<const wchar_t *>(text.data()),
            int(text.size()), bytes, sizeof(bytes), nullptr, &defaulted);
        *substituted = defaulted != FALSE || size <= 0;
        return std::string(bytes, size_t(size > 0 ? size : 0));
    }

    void decodingAgrees(UINT page) {
        int differences = 0;
        for (int b = 0; b < 256; ++b) {
            for (int t = -1; t < 256; ++t) {
                std::string bytes(1, char(b));
                if (t >= 0) {
                    bytes += char(t);
                }
                const auto ours = winacp::decode(int(page), bytes);
                const auto theirs = windowsDecode(page, bytes);
                if (ours != theirs && ++differences <= 5) {
                    std::printf("code page %u: bytes %02X %02X decode differently\n", page, b,
                                t < 0 ? 0 : t);
                }
            }
        }
        CHECK(differences == 0);
    }

    void encodingAgrees(UINT page) {
        int differences = 0;
        for (int c = 0; c < 0x10000; ++c) {
            if (c >= 0xD800 && c < 0xE000) {
                continue;
            }
            const std::u16string text(1, char16_t(c));
            bool substituted = false;
            const std::string theirs = windowsEncode(page, text, &substituted);
            const auto ours = winacp::encode(int(page), text);
            const bool agrees = substituted ? !ours.has_value() : (ours && *ours == theirs);
            // Where Windows substitutes its default character, the replacing overload must
            // produce the same output.
            const bool replacedAlike =
                !substituted || winacp::encode(int(page), text, '?') == theirs;
            if ((!agrees || !replacedAlike) && ++differences <= 5) {
                std::printf("code page %u: U+%04X encodes differently\n", page, c);
            }
        }
        CHECK(differences == 0);

        // A character outside the Basic Multilingual Plane.
        bool substituted = false;
        const std::u16string astral = u"a\U0001F600b";
        CHECK(winacp::encode(int(page), astral, '?') == windowsEncode(page, astral, &substituted));
    }

}

int main() {
    for (const int page : winacp::codePages()) {
        decodingAgrees(UINT(page));
        encodingAgrees(UINT(page));
    }
    return CHECK_RESULT();
}
