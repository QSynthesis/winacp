// Table generator: writes the tables in tables/ using the Windows code page API.
//
//     winacp-generate <directory>
//
// Windows only. After regeneration, run test_against_windows to verify that the tables match the
// API. The table format is specified in src/winacp.cpp.
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

    void put16(std::vector<unsigned char> &out, std::uint16_t value) {
        out.push_back(static_cast<unsigned char>(value & 0xFF));
        out.push_back(static_cast<unsigned char>(value >> 8));
    }

    void put32(std::vector<unsigned char> &out, std::uint32_t value) {
        put16(out, static_cast<std::uint16_t>(value & 0xFFFF));
        put16(out, static_cast<std::uint16_t>(value >> 16));
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

        std::vector<unsigned char> out;
        out.insert(out.end(), {'W', 'A', 'C', 'P'});
        put16(out, 1);
        put16(out, static_cast<std::uint16_t>(page));
        put16(out, static_cast<std::uint16_t>(leads.size()));
        put16(out, 0);
        for (int b = 0; b < 256; ++b) {
            out.push_back(lead[b] ? 1 : 0);
        }
        for (const auto c : single) {
            put16(out, c);
        }
        for (const auto c : rows) {
            put16(out, c);
        }
        put32(out, static_cast<std::uint32_t>(overrides.size()));
        for (const auto &[c, bytes] : overrides) {
            put16(out, c);
            put16(out, bytes);
        }

        const std::string path = directory + "/cp" + std::to_string(page) + ".bin";
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(out.data()), std::streamsize(out.size()));
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
