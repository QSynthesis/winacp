#include <winacp/winacp.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "tables.h"

// Table format, as written by tools/generate. All integers are little-endian.
//
// - Header: the magic "WACP", followed by four 16-bit fields: format version (1), code page,
//   number of lead bytes, and a reserved zero.
// - Lead byte flags: 256 bytes, nonzero for each byte that begins a two-byte sequence.
// - Single-byte map: 256 code units, the decoding of each byte that is not a lead byte.
// - Double-byte rows: one row of 256 code units per lead byte, in ascending order of lead byte,
//   giving the decoding of each trail byte after that lead byte.
// - Encoding overrides: a 32-bit count, followed by that many pairs of a code unit and the
//   sequence Windows encodes it to, packed as (lead << 8) | trail. An override is recorded where
//   Windows encodes a character to a sequence other than the first in byte order that decodes
//   to it, and where Windows encodes a character to which no sequence decodes, which occurs for
//   several private use characters on code pages 1255 and 1257.
//
// The value 0xFFFF, a noncharacter, denotes both an invalid sequence and an unencodable
// character. Every supported code page maps into the Basic Multilingual Plane only, so each
// character is a single code unit.

namespace winacp {

    namespace {

        constexpr std::uint16_t none = 0xFFFF;

        struct Table {
            bool lead[256] = {};
            int row[256] = {}; // row index of each lead byte
            const unsigned char *single = nullptr;
            const unsigned char *rows = nullptr;
            const unsigned char *overrides = nullptr;
            std::uint32_t overrideCount = 0;

            std::uint16_t at(const unsigned char *base, std::size_t index) const {
                return std::uint16_t(base[index * 2] | (base[index * 2 + 1] << 8));
            }

            std::uint16_t decodeSingle(unsigned b) const {
                return at(single, b);
            }

            std::uint16_t decodePair(unsigned b, unsigned t) const {
                return at(rows, std::size_t(row[b]) * 256 + t);
            }

            // Encoding map, built on first use and retained.
            mutable std::once_flag backOnce;
            mutable std::unique_ptr<std::uint16_t[]> back;

            const std::uint16_t *backward() const {
                std::call_once(backOnce, [this] {
                    back.reset(new std::uint16_t[65536]);
                    std::fill(back.get(), back.get() + 65536, none);
                    for (unsigned b = 0; b < 256; ++b) {
                        const auto c = decodeSingle(b);
                        if (!lead[b] && c != none && back[c] == none) {
                            back[c] = std::uint16_t(b);
                        }
                    }
                    for (unsigned b = 0; b < 256; ++b) {
                        if (!lead[b]) {
                            continue;
                        }
                        for (unsigned t = 0; t < 256; ++t) {
                            const auto c = decodePair(b, t);
                            if (c != none && back[c] == none) {
                                back[c] = std::uint16_t((b << 8) | t);
                            }
                        }
                    }
                    for (std::uint32_t k = 0; k < overrideCount; ++k) {
                        back[at(overrides, k * 2)] = at(overrides, k * 2 + 1);
                    }
                });
                return back.get();
            }
        };

        /// Parses \a blob , returning null if it is not a well-formed table of its code page.
        /// The data is embedded at build time, so a malformed table indicates a build error; it
        /// is reported by absence rather than by reading out of bounds.
        std::unique_ptr<Table> read(const detail::Blob &blob) {
            const unsigned char *data = blob.data;
            const std::size_t size = blob.size;
            const auto u16 = [data](std::size_t i) {
                return std::uint16_t(data[i] | (data[i + 1] << 8));
            };
            if (size < 12 || data[0] != 'W' || data[1] != 'A' || data[2] != 'C' || data[3] != 'P' ||
                u16(4) != 1 || u16(6) != blob.codePage) {
                return nullptr;
            }
            const std::size_t leads = u16(8);
            const std::size_t fixed = 12 + 256 + 512 + leads * 512 + 4;
            if (size < fixed) {
                return nullptr;
            }

            auto table = std::make_unique<Table>();
            std::size_t counted = 0;
            for (unsigned b = 0; b < 256; ++b) {
                table->lead[b] = data[12 + b] != 0;
                if (table->lead[b]) {
                    // Bytes below 0x80 are ASCII on every supported code page and cannot be
                    // lead bytes.
                    if (b < 0x80) {
                        return nullptr;
                    }
                    table->row[b] = int(counted++);
                }
            }
            if (counted != leads) {
                return nullptr;
            }
            table->single = data + 12 + 256;
            table->rows = table->single + 512;
            const std::size_t countAt = 12 + 256 + 512 + leads * 512;
            table->overrideCount =
                std::uint32_t(u16(countAt)) | (std::uint32_t(u16(countAt + 2)) << 16);
            if (size != fixed + std::size_t(table->overrideCount) * 4) {
                return nullptr;
            }
            table->overrides = data + countAt + 4;
            return table;
        }

        /// Returns the table of the code page numbered \a number , or null if no supported
        /// code page has that number. All tables are parsed on first use.
        const Table *tableOf(int number) {
            static const auto tables = [] {
                std::vector<std::pair<int, std::unique_ptr<Table>>> out;
                for (std::size_t i = 0; i < detail::blobCount; ++i) {
                    auto table = read(detail::blobs[i]);
                    if (table) {
                        out.emplace_back(detail::blobs[i].codePage, std::move(table));
                    }
                }
                return out;
            }();
            for (const auto &[page, table] : tables) {
                if (page == number) {
                    return table.get();
                }
            }
            return nullptr;
        }

    }

    std::optional<CodePage> codePageFromNumber(int number) {
        if (!tableOf(number)) {
            return std::nullopt;
        }
        return CodePage(number);
    }

    const std::vector<CodePage> &codePages() {
        static const auto pages = [] {
            std::vector<CodePage> out;
            for (std::size_t i = 0; i < detail::blobCount; ++i) {
                if (tableOf(detail::blobs[i].codePage)) {
                    out.push_back(CodePage(detail::blobs[i].codePage));
                }
            }
            return out;
        }();
        return pages;
    }

    std::optional<std::u16string> decode(CodePage codePage, std::string_view bytes) {
        const Table *table = tableOf(toNumber(codePage));
        if (!table) {
            return std::nullopt;
        }
        std::u16string text;
        text.reserve(bytes.size());
        const auto *at = reinterpret_cast<const unsigned char *>(bytes.data());
        const std::size_t size = bytes.size();
        for (std::size_t i = 0; i < size; ++i) {
            const unsigned b = at[i];
            std::uint16_t c;
            if (table->lead[b]) {
                if (i + 1 >= size) {
                    return std::nullopt;
                }
                c = table->decodePair(b, at[++i]);
            } else {
                c = table->decodeSingle(b);
            }
            if (c == none) {
                return std::nullopt;
            }
            text += char16_t(c);
        }
        return text;
    }

    namespace {

        /// Encodes \a text into \a out . Returns false at the first unencodable character if
        /// \a replacement is null.
        bool write(const Table &table, std::u16string_view text, const char *replacement,
                   std::string &out) {
            const std::uint16_t *back = table.backward();
            out.reserve(text.size() * 2);
            for (std::size_t i = 0; i < text.size(); ++i) {
                const char16_t c = text[i];
                const std::uint16_t packed = back[c];
                if (packed == none) {
                    if (!replacement) {
                        return false;
                    }
                    // One replacement per code unit, as Windows emits; a character outside the
                    // Basic Multilingual Plane therefore yields two.
                    out += *replacement;
                } else if (packed > 0xFF) {
                    out += char(packed >> 8);
                    out += char(packed & 0xFF);
                } else {
                    out += char(packed);
                }
            }
            return true;
        }

    }

    std::optional<std::string> encode(CodePage codePage, std::u16string_view text) {
        const Table *table = tableOf(toNumber(codePage));
        if (!table) {
            return std::nullopt;
        }
        std::string out;
        if (!write(*table, text, nullptr, out)) {
            return std::nullopt;
        }
        return out;
    }

    std::string encode(CodePage codePage, std::u16string_view text, char replacement) {
        const Table *table = tableOf(toNumber(codePage));
        std::string out;
        if (table) {
            write(*table, text, &replacement, out);
        }
        return out;
    }

}
