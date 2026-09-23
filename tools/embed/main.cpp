// Table compiler: converts the text tables in tables/ into a C++ source of arrays for the library.
//
//     winacp-embed <output> <table>...
//
// Run by the build. The tables are validated here, so that the library contains no parser and
// indexes the arrays directly. The output is written only if its content changed.
//
// Table format, as written by tools/generate: UTF-8 text, with LF or CRLF line endings.
//
// - The first line is "winacp 1 <code page>": the format version and the code page, which must
//   match the file name cp<code page>.txt.
// - Empty lines and lines beginning with '#' are ignored.
// - A line "<prefix> <cells>" gives the decoding of 16 consecutive byte sequences. A prefix of two
//   hexadecimal digits denotes single bytes starting at that value. A prefix of four digits
//   denotes the two-byte sequences of one lead byte, starting at that trail byte. The last byte
//   of every prefix is a multiple of 16, and each prefix occurs once.
// - A cell is a character, '.' for an invalid sequence, '^' for a lead byte on a single-byte
//   line, or \uXXXX for the code unit XXXX in hexadecimal. The generator escapes controls,
//   spaces, format and nonspacing characters, and the characters '.', '^' and '\'. Any character
//   may be escaped.
// - A line "= <code unit> <sequence> <cell>" is an encoding override: Windows encodes the code
//   unit to the sequence, given in hexadecimal as two or four digits, rather than to the first
//   sequence in byte order that decodes to it. The cell repeats the code unit for reading and
//   must match it. An override is also recorded if Windows encodes a character to which no
//   sequence decodes, which occurs for several private use characters on code pages 1255 and
//   1257.
//
// All 16 single-byte lines must be present, and all 16 lines of each lead byte. Lead bytes are
// at least 0x80, because bytes below 0x80 are ASCII on every supported code page.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    constexpr std::uint16_t none = 0xFFFF;

    // A cell value that is not a code unit.
    constexpr std::uint32_t leadMark = 0x10000;

    struct Table {
        int codePage = 0;
        bool lead[256] = {};
        std::uint16_t single[256] = {};
        std::vector<std::uint16_t> pairs = std::vector<std::uint16_t>(65536, none);
        std::vector<std::pair<std::uint16_t, std::uint16_t>> overrides;
    };

    /// Parses \a digits hexadecimal digits at the start of \a text . Uppercase and lowercase
    /// digits are accepted.
    std::optional<std::uint32_t> parseHex(std::string_view text, std::size_t digits) {
        if (text.size() < digits) {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < digits; ++i) {
            const char ch = text[i];
            std::uint32_t digit;
            if (ch >= '0' && ch <= '9') {
                digit = std::uint32_t(ch - '0');
            } else if (ch >= 'A' && ch <= 'F') {
                digit = std::uint32_t(ch - 'A' + 10);
            } else if (ch >= 'a' && ch <= 'f') {
                digit = std::uint32_t(ch - 'a' + 10);
            } else {
                return std::nullopt;
            }
            value = value * 16 + digit;
        }
        return value;
    }

    /// Parses one cell at the start of \a text and removes it from \a text .
    ///
    /// \return the code unit, \c none for an invalid sequence, \c leadMark for a lead byte, or
    ///         \c std::nullopt if the cell is malformed. A character outside the Basic
    ///         Multilingual Plane, a surrogate and an overlong encoding are malformed.
    std::optional<std::uint32_t> takeCell(std::string_view &text) {
        if (text.empty()) {
            return std::nullopt;
        }
        const auto b0 = static_cast<unsigned char>(text[0]);
        if (b0 == '.') {
            text.remove_prefix(1);
            return none;
        }
        if (b0 == '^') {
            text.remove_prefix(1);
            return leadMark;
        }
        if (b0 == '\\') {
            if (text.size() < 6 || text[1] != 'u') {
                return std::nullopt;
            }
            const auto value = parseHex(text.substr(2), 4);
            text.remove_prefix(6);
            return value;
        }
        if (b0 < 0x80) {
            text.remove_prefix(1);
            return b0;
        }
        std::size_t length;
        std::uint32_t c;
        if ((b0 & 0xE0) == 0xC0) {
            length = 2;
            c = b0 & 0x1F;
        } else if ((b0 & 0xF0) == 0xE0) {
            length = 3;
            c = b0 & 0x0F;
        } else {
            return std::nullopt;
        }
        if (text.size() < length) {
            return std::nullopt;
        }
        for (std::size_t i = 1; i < length; ++i) {
            const auto b = static_cast<unsigned char>(text[i]);
            if ((b & 0xC0) != 0x80) {
                return std::nullopt;
            }
            c = (c << 6) | (b & 0x3F);
        }
        if ((length == 2 && c < 0x80) || (length == 3 && c < 0x800) ||
            (c >= 0xD800 && c < 0xE000)) {
            return std::nullopt;
        }
        text.remove_prefix(length);
        return c;
    }

    /// Reports an error at line \a line of \a path and returns false.
    bool fail(const std::string &path, int line, const std::string &message) {
        std::fprintf(stderr, "%s:%d: %s\n", path.c_str(), line, message.c_str());
        return false;
    }

    /// Returns the code page of a table file named cp<number>.txt, or \c std::nullopt if the name
    /// has another form.
    std::optional<int> codePageOf(const std::filesystem::path &path) {
        const std::string name = path.filename().string();
        if (name.size() < 7 || name.compare(0, 2, "cp") != 0 ||
            name.compare(name.size() - 4, 4, ".txt") != 0) {
            return std::nullopt;
        }
        const std::string digits = name.substr(2, name.size() - 6);
        if (digits.empty() || digits.size() > 5 ||
            !std::all_of(digits.begin(), digits.end(),
                         [](char ch) { return ch >= '0' && ch <= '9'; })) {
            return std::nullopt;
        }
        return std::stoi(digits);
    }

    /// Reads and validates the table at \a path into \a table . Reports the first error and
    /// returns false if the table is malformed.
    bool read(const std::string &path, Table &table) {
        const auto codePage = codePageOf(path);
        if (!codePage) {
            return fail(path, 0, "file name does not match cp<number>.txt");
        }
        table.codePage = *codePage;

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return fail(path, 0, "cannot open the file");
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        const std::string content = buffer.str();
        std::string_view text(content);

        std::fill(std::begin(table.single), std::end(table.single), none);
        std::uint16_t pairLines[256] = {};
        std::uint16_t singleLines = 0;

        int number = 0;
        while (!text.empty()) {
            const std::size_t end = text.find('\n');
            std::string_view line = text.substr(0, end);
            text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            ++number;

            if (number == 1) {
                const std::string expected = "winacp 1 " + std::to_string(table.codePage);
                if (line != expected) {
                    return fail(path, number, "the first line must be '" + expected + "'");
                }
                continue;
            }
            if (line.empty() || line[0] == '#') {
                continue;
            }

            if (line[0] == '=') {
                // "= XXXX YY c" or "= XXXX YYYY c"
                const auto c = line.size() >= 10 && line[1] == ' ' && line[6] == ' '
                                   ? parseHex(line.substr(2), 4)
                                   : std::nullopt;
                const std::size_t space = line.find(' ', 7);
                if (!c || (space != 9 && space != 11)) {
                    return fail(path, number, "malformed override");
                }
                const auto sequence = parseHex(line.substr(7), space - 7);
                std::string_view rest = line.substr(space + 1);
                const auto shown = takeCell(rest);
                if (!sequence || !shown || !rest.empty()) {
                    return fail(path, number, "malformed override");
                }
                if (*shown != *c) {
                    return fail(path, number, "the character does not match the code unit");
                }
                table.overrides.emplace_back(std::uint16_t(*c), std::uint16_t(*sequence));
                continue;
            }

            const std::size_t space = line.find(' ');
            const auto prefix =
                space == 2 || space == 4 ? parseHex(line, space) : std::optional<std::uint32_t>();
            if (!prefix || (*prefix & 0x0F) != 0) {
                return fail(path, number, "malformed prefix");
            }
            std::string_view cells = line.substr(space + 1);
            std::uint32_t values[16];
            for (auto &value : values) {
                const auto cell = takeCell(cells);
                if (!cell) {
                    return fail(path, number, "malformed cell, or fewer than 16 cells");
                }
                value = *cell;
            }
            if (!cells.empty()) {
                return fail(path, number, "more than 16 cells");
            }

            if (space == 2) {
                const unsigned start = *prefix;
                const auto bit = std::uint16_t(1u << (start >> 4));
                if (singleLines & bit) {
                    return fail(path, number, "duplicate line");
                }
                singleLines |= bit;
                for (unsigned k = 0; k < 16; ++k) {
                    if (values[k] == leadMark) {
                        table.lead[start + k] = true;
                    } else {
                        table.single[start + k] = std::uint16_t(values[k]);
                    }
                }
            } else {
                const unsigned b = *prefix >> 8;
                const unsigned start = *prefix & 0xFF;
                const auto bit = std::uint16_t(1u << (start >> 4));
                if (pairLines[b] & bit) {
                    return fail(path, number, "duplicate line");
                }
                pairLines[b] |= bit;
                for (unsigned k = 0; k < 16; ++k) {
                    if (values[k] == leadMark) {
                        return fail(path, number, "'^' on a two-byte line");
                    }
                    table.pairs[(b << 8) | (start + k)] = std::uint16_t(values[k]);
                }
            }
        }
        if (number == 0) {
            return fail(path, 0, "empty file");
        }
        if (singleLines != 0xFFFF) {
            return fail(path, number, "a single-byte line is missing");
        }
        for (unsigned b = 0; b < 256; ++b) {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "%02X", b);
            if (!table.lead[b] && pairLines[b] != 0) {
                return fail(path, number,
                            std::string("lines for ") + hex + ", which is not a lead byte");
            }
            if (table.lead[b] && b < 0x80) {
                return fail(path, number, std::string("lead byte ") + hex + " is below 0x80");
            }
            if (table.lead[b] && pairLines[b] != 0xFFFF) {
                return fail(path, number,
                            std::string("a line of lead byte ") + hex + " is missing");
            }
        }
        return true;
    }

    /// Appends \a count values as a C++ array initializer, 16 per line to match the tables.
    template <class T>
    void appendValues(std::string &out, const T *values, std::size_t count, int digits) {
        for (std::size_t i = 0; i < count; ++i) {
            char text[16];
            std::snprintf(text, sizeof(text), "0x%0*X,", digits, unsigned(values[i]));
            out += (i % 16 == 0) ? "\n            " : " ";
            out += text;
        }
    }

    std::string source(const std::vector<Table> &tables) {
        std::string out = "// Generated from tables/ by tools/embed. Do not edit.\n\n"
                          "#include \"tables.h\"\n\n"
                          "namespace winacp::detail {\n\n"
                          "    namespace {\n";
        for (const Table &table : tables) {
            const std::string name = "cp" + std::to_string(table.codePage);

            std::uint8_t rowOf[256] = {};
            std::vector<std::uint16_t> rows;
            int count = 0;
            for (unsigned b = 0; b < 256; ++b) {
                if (table.lead[b]) {
                    rowOf[b] = std::uint8_t(++count);
                    rows.insert(rows.end(), table.pairs.begin() + (b << 8),
                                table.pairs.begin() + (b << 8) + 256);
                }
            }

            out += "\n        const std::uint16_t " + name + "Single[256] = {";
            appendValues(out, table.single, 256, 4);
            out += "\n        };\n";
            out += "\n        const std::uint8_t " + name + "RowOf[256] = {";
            appendValues(out, rowOf, 256, 2);
            out += "\n        };\n";
            if (!rows.empty()) {
                out += "\n        const std::uint16_t " + name + "Rows[" +
                       std::to_string(rows.size()) + "] = {";
                appendValues(out, rows.data(), rows.size(), 4);
                out += "\n        };\n";
            }
            if (!table.overrides.empty()) {
                out += "\n        const std::uint16_t " + name + "Overrides[][2] = {";
                for (const auto &[c, sequence] : table.overrides) {
                    char text[32];
                    std::snprintf(text, sizeof(text), "\n            {0x%04X, 0x%04X},", c,
                                  sequence);
                    out += text;
                }
                out += "\n        };\n";
            }
        }
        out += "\n    }\n\n    const TableData tables[] = {\n";
        for (const Table &table : tables) {
            const std::string name = "cp" + std::to_string(table.codePage);
            const bool hasRows = std::any_of(std::begin(table.lead), std::end(table.lead),
                                             [](bool lead) { return lead; });
            out += "        {" + std::to_string(table.codePage) + ", " + name + "Single, " + name +
                   "RowOf, " + (hasRows ? name + "Rows" : std::string("nullptr")) + ", " +
                   (table.overrides.empty() ? std::string("nullptr") : name + "Overrides") + ", " +
                   std::to_string(table.overrides.size()) + "},\n";
        }
        out += "    };\n\n    const std::size_t tableCount = " + std::to_string(tables.size()) +
               ";\n\n}\n";
        return out;
    }

}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: winacp-embed <output> <table>...\n");
        return 2;
    }

    std::vector<Table> tables(std::size_t(argc - 2));
    for (int i = 2; i < argc; ++i) {
        if (!read(argv[i], tables[std::size_t(i - 2)])) {
            return 1;
        }
    }
    std::sort(tables.begin(), tables.end(),
              [](const Table &a, const Table &b) { return a.codePage < b.codePage; });
    for (std::size_t i = 1; i < tables.size(); ++i) {
        if (tables[i].codePage == tables[i - 1].codePage) {
            std::fprintf(stderr, "code page %d occurs twice\n", tables[i].codePage);
            return 1;
        }
    }

    const std::string out = source(tables);
    {
        std::ifstream existing(argv[1], std::ios::binary);
        if (existing) {
            std::ostringstream buffer;
            buffer << existing.rdbuf();
            if (buffer.str() == out) {
                return 0;
            }
        }
    }
    std::ofstream file(argv[1], std::ios::binary | std::ios::trunc);
    file.write(out.data(), std::streamsize(out.size()));
    if (!file) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
