// Table generator: writes the tables in tables/ using the Windows code page API.
//
//     winacp-generate <directory>
//
// Windows only. After regeneration, run test_against_windows to verify that the tables match the
// API. The table format is specified in tools/embed and summarized at the top of each table.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

namespace {

    constexpr std::uint16_t none = 0xFFFF;

    // All code pages that Windows uses as the ANSI code page of a locale.
    constexpr UINT codePages[] = {874,  932,  936,  949,  950,  1250, 1251,
                                  1252, 1253, 1254, 1255, 1256, 1257, 1258};

    /// Returns whether \a c is written as an escape rather than as the character itself: a
    /// control, a space, a format or nonspacing character, or one of the characters that the
    /// format uses as markers. Any character may be escaped, so this affects readability only.
    bool escaped(std::uint16_t c) {
        if (c < 0x21 || (c >= 0x7F && c <= 0xA0) || c == '.' || c == '^' || c == '\\' ||
            c == 0xAD || c == 0xFEFF || (c >= 0x2000 && c <= 0x200F) ||
            (c >= 0x2028 && c <= 0x202F) || (c >= 0x205F && c <= 0x206F)) {
            return true;
        }
        const wchar_t w = static_cast<wchar_t>(c);
        WORD type1 = 0;
        WORD type3 = 0;
        GetStringTypeW(CT_CTYPE1, &w, 1, &type1);
        GetStringTypeW(CT_CTYPE3, &w, 1, &type3);
        return (type1 & (C1_CNTRL | C1_SPACE | C1_BLANK)) != 0 || (type3 & C3_NONSPACING) != 0;
    }

    /// Appends the cell of \a c : the character in UTF-8, an escape, or '.' if none.
    void appendCell(std::string &out, std::uint16_t c) {
        if (c == none) {
            out += '.';
        } else if (escaped(c)) {
            char escape[8];
            std::snprintf(escape, sizeof(escape), "\\u%04X", c);
            out += escape;
        } else if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }

    std::string hex(unsigned value, int digits) {
        char text[8];
        std::snprintf(text, sizeof(text), "%0*X", digits, value);
        return text;
    }

    /// Returns the character that \a bytes decode to, or none if they do not decode to exactly
    /// one character.
    std::uint16_t decode(UINT page, const unsigned char *bytes, int size) {
        wchar_t text[4];
        const int length = MultiByteToWideChar(
            page, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(bytes), size, text, 4);
        return length == 1 ? static_cast<std::uint16_t>(text[0]) : none;
    }

    /// Returns the sequence that \a c encodes to, packed as in the table, or none if Windows
    /// substitutes the default character.
    std::uint16_t encode(UINT page, wchar_t c) {
        unsigned char bytes[4];
        BOOL defaulted = FALSE;
        const int size =
            WideCharToMultiByte(page, WC_NO_BEST_FIT_CHARS, &c, 1, reinterpret_cast<char *>(bytes),
                                4, nullptr, &defaulted);
        if (defaulted || size < 1 || size > 2) {
            return none;
        }
        return size == 1 ? bytes[0] : static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1]);
    }

    bool write(UINT page, const std::string &directory) {
        CPINFO info{};
        if (!GetCPInfo(page, &info)) {
            std::fprintf(stderr, "code page %u is not installed\n", page);
            return false;
        }

        bool lead[256] = {};
        for (int r = 0; r < MAX_LEADBYTES && info.LeadByte[r]; r += 2) {
            for (int b = info.LeadByte[r]; b <= info.LeadByte[r + 1]; ++b) {
                lead[b] = true;
            }
        }
        std::vector<int> leads;
        for (int b = 0; b < 256; ++b) {
            if (lead[b]) {
                leads.push_back(b);
            }
        }

        std::vector<std::uint16_t> single(256, none);
        for (int b = 0; b < 256; ++b) {
            if (!lead[b]) {
                const unsigned char one[1] = {static_cast<unsigned char>(b)};
                single[b] = decode(page, one, 1);
            }
        }
        std::vector<std::uint16_t> rows(leads.size() * 256, none);
        for (size_t r = 0; r < leads.size(); ++r) {
            for (int t = 0; t < 256; ++t) {
                const unsigned char two[2] = {static_cast<unsigned char>(leads[r]),
                                              static_cast<unsigned char>(t)};
                rows[r * 256 + t] = decode(page, two, 2);
            }
        }

        // The default encoding of a character is the first sequence in byte order that decodes
        // to it. Overrides are recorded where Windows encodes a character differently, and for
        // characters to which no sequence decodes but which Windows encodes nevertheless
        // (several private use characters on code pages 1255 and 1257).
        std::vector<std::uint16_t> first(65536, none);
        for (int b = 0; b < 256; ++b) {
            if (single[b] != none && first[single[b]] == none) {
                first[single[b]] = static_cast<std::uint16_t>(b);
            }
        }
        for (size_t r = 0; r < leads.size(); ++r) {
            for (int t = 0; t < 256; ++t) {
                const std::uint16_t c = rows[r * 256 + t];
                if (c != none && first[c] == none) {
                    first[c] = static_cast<std::uint16_t>((leads[r] << 8) | t);
                }
            }
        }
        std::vector<std::pair<std::uint16_t, std::uint16_t>> overrides;
        for (int c = 0; c < 65536; ++c) {
            if (c >= 0xD800 && c < 0xE000) {
                continue;
            }
            const std::uint16_t windows = encode(page, static_cast<wchar_t>(c));
            if (first[c] == none) {
                if (windows != none) {
                    overrides.emplace_back(static_cast<std::uint16_t>(c), windows);
                }
                continue;
            }
            if (windows == none) {
                // On every supported code page, each decodable character is also encodable.
                // The format cannot represent the contrary case, so it is treated as an error.
                std::fprintf(stderr, "code page %u: U+%04X is decodable but not encodable\n", page,
                             c);
                return false;
            }
            if (windows != first[c]) {
                overrides.emplace_back(static_cast<std::uint16_t>(c), windows);
            }
        }

        std::string out = "winacp 1 " + std::to_string(page) + "\n";
        out +=
            "# Code page " + std::to_string(page) +
            ", generated by tools/generate from the Windows API.\n"
            "#\n"
            "# Each line gives the characters of 16 consecutive byte sequences, starting at the\n"
            "# hexadecimal prefix: two digits for single bytes, four for the sequences of one\n"
            "# lead byte. '.' is an invalid sequence, '^' a lead byte, and \\uXXXX the character\n"
            "# with that code unit. A line '= XXXX YYYY c' records that Windows encodes the\n"
            "# character XXXX to the sequence YYYY rather than to the first sequence that\n"
            "# decodes to it.\n";

        out += '\n';
        for (int start = 0; start < 256; start += 16) {
            out += hex(unsigned(start), 2) + ' ';
            for (int b = start; b < start + 16; ++b) {
                if (lead[b]) {
                    out += '^';
                } else {
                    appendCell(out, single[b]);
                }
            }
            out += '\n';
        }
        for (size_t r = 0; r < leads.size(); ++r) {
            out += '\n';
            for (int start = 0; start < 256; start += 16) {
                out += hex(unsigned(leads[r]), 2) + hex(unsigned(start), 2) + ' ';
                for (int t = start; t < start + 16; ++t) {
                    appendCell(out, rows[r * 256 + t]);
                }
                out += '\n';
            }
        }
        if (!overrides.empty()) {
            out += '\n';
            for (const auto &[c, bytes] : overrides) {
                out += "= " + hex(c, 4) + ' ' + hex(bytes, bytes > 0xFF ? 4 : 2) + ' ';
                appendCell(out, c);
                out += '\n';
            }
        }

        const std::string path = directory + "/cp" + std::to_string(page) + ".txt";
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(out.data(), std::streamsize(out.size()));
        if (!file) {
            std::fprintf(stderr, "cannot write %s\n", path.c_str());
            return false;
        }
        std::printf("%s: %zu bytes, %zu lead bytes, %zu encoding overrides\n", path.c_str(),
                    out.size(), leads.size(), overrides.size());
        return true;
    }

}

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: winacp-generate <directory>\n");
        return 2;
    }
    bool ok = true;
    for (const UINT page : codePages) {
        ok = write(page, argv[1]) && ok;
    }
    return ok ? 0 : 1;
}
