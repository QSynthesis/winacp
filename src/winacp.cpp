#include "winacp.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>

#include "tables.h"

// The tables are compiled from tables/ into arrays by tools/embed, which also specifies and
// validates the text format. The value 0xFFFF, a noncharacter, denotes both an invalid sequence
// and an unencodable character. Every supported code page maps into the Basic Multilingual Plane
// only, so each character is a single code unit.

namespace winacp {

    namespace {

        constexpr std::uint16_t none = 0xFFFF;

        struct Table {
            const detail::TableData *data = nullptr;

            bool isLead(unsigned b) const {
                return data->rowOf[b] != 0;
            }

            std::uint16_t decodeSingle(unsigned b) const {
                return data->single[b];
            }

            std::uint16_t decodePair(unsigned b, unsigned t) const {
                return data->rows[std::size_t(data->rowOf[b] - 1) * 256 + t];
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
                        if (!isLead(b) && c != none && back[c] == none) {
                            back[c] = std::uint16_t(b);
                        }
                    }
                    for (unsigned b = 0; b < 256; ++b) {
                        if (!isLead(b)) {
                            continue;
                        }
                        for (unsigned t = 0; t < 256; ++t) {
                            const auto c = decodePair(b, t);
                            if (c != none && back[c] == none) {
                                back[c] = std::uint16_t((b << 8) | t);
                            }
                        }
                    }
                    for (std::size_t k = 0; k < data->overrideCount; ++k) {
                        back[data->overrides[k][0]] = data->overrides[k][1];
                    }
                });
                return back.get();
            }
        };

        /// Returns the table of the code page numbered \a number , or null if no supported
        /// code page has that number.
        const Table *tableOf(int number) {
            static const std::unique_ptr<Table[]> tables = [] {
                std::unique_ptr<Table[]> out(new Table[detail::tableCount]);
                for (std::size_t i = 0; i < detail::tableCount; ++i) {
                    out[i].data = &detail::tables[i];
                }
                return out;
            }();
            for (std::size_t i = 0; i < detail::tableCount; ++i) {
                if (detail::tables[i].codePage == number) {
                    return &tables[i];
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
            for (std::size_t i = 0; i < detail::tableCount; ++i) {
                out.push_back(CodePage(detail::tables[i].codePage));
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
            if (table->isLead(b)) {
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
