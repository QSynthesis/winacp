// Platform-independent tests against fixed expected values.
//
// test_against_windows compares the tables with the Windows API. This test runs on all
// platforms and verifies that the embedded tables are intact where that API is unavailable.
#include <string>

#include <winacp/winacp.h>

#include "check.h"

using namespace std::string_literals;

namespace {

    bool decodes(int page, const std::string &bytes, const std::u16string &text) {
        const auto got = winacp::decode(page, bytes);
        return got && *got == text;
    }

    bool encodes(int page, const std::u16string &text, const std::string &bytes) {
        const auto got = winacp::encode(page, text);
        return got && *got == bytes;
    }

    void everyPageIsHeld() {
        const int expected[] = {874,  932,  936,  949,  950,  1250, 1251,
                                1252, 1253, 1254, 1255, 1256, 1257, 1258};
        CHECK(winacp::codePages().size() == sizeof(expected) / sizeof(expected[0]));
        for (const int page : expected) {
            CHECK(winacp::isAvailable(page));
        }
        CHECK(!winacp::isAvailable(65001));
        CHECK(!winacp::isAvailable(54936));
        CHECK(!winacp::decode(65001, "a"));
        CHECK(!winacp::encode(65001, u"a"));
        CHECK(winacp::encode(65001, u"a", '?').empty());
    }

    // One representative character per code page, as converted by Windows.
    void theCharactersAreWhereWindowsHasThem() {
        CHECK(decodes(932, "\x82\xa0"s, u"\u3042")); // あ
        CHECK(encodes(932, u"\u3042", "\x82\xa0"s));
        CHECK(decodes(936, "\xc4\xe3"s, u"\u4f60")); // 你
        CHECK(decodes(936, "\x80"s, u"\u20ac"));     // €, absent from standard GBK
        CHECK(decodes(949, "\xc7\xd1"s, u"\ud55c")); // 한
        CHECK(decodes(950, "\xa4\xa4"s, u"\u4e2d")); // 中
        CHECK(decodes(1251, "\xc0"s, u"\u0410"));    // А
        CHECK(decodes(1252, "\x80"s, u"\u20ac"));    // €
        CHECK(decodes(874, "\xa1"s, u"\u0e01"));     // ก
        CHECK(decodes(932, "\\~"s, u"\\~"));         // ASCII, not YEN SIGN or OVERLINE
    }

    // User-defined rows, which Windows maps into the Private Use Area. ICU converts these
    // incorrectly on code page 932, and the macOS converters omit them.
    void theUserDefinedRowsAreThere() {
        CHECK(decodes(932, "\xf0\x40"s, u"\ue000"));
        CHECK(decodes(936, "\xaa\xa1"s, u"\ue000"));
        CHECK(decodes(950, "\xfa\x40"s, u"\ue000"));
        CHECK(encodes(950, u"\ue000", "\xfa\x40"s));
    }

    // A character with both an NEC and an IBM encoding is encoded as by Windows, regardless of
    // the sequence it was decoded from.
    void aCharacterWithTwoSpellingsIsWrittenAsWindowsWritesIt() {
        CHECK(decodes(932, "\xed\x40"s, u"\u7e8a"));
        CHECK(decodes(932, "\xfa\x5c"s, u"\u7e8a"));
        CHECK(encodes(932, u"\u7e8a", "\xfa\x5c"s));
        CHECK(decodes(932, "\x87\x90"s, u"\u2252")); // NEC row 13; encoded to JIS row 2
        CHECK(encodes(932, u"\u2252", "\x81\xe0"s));
    }

    // Invalid sequences are rejected rather than replaced.
    void bytesThatDoNotDecodeAreRefused() {
        CHECK(!winacp::decode(932, "\x82"s)); // truncated sequence
        CHECK(!winacp::decode(932, "a\x82"s));
        CHECK(!winacp::decode(932, "\x82\x20"s)); // invalid trail byte
        CHECK(!winacp::decode(932, "\xa0"s));
        CHECK(!winacp::decode(936, "\xff"s));
        CHECK(decodes(932, ""s, u""));
    }

    void whatAPageCannotHoldIsRefusedOrReplaced() {
        CHECK(!winacp::encode(932, u"\u4f60")); // 你, absent from code page 932
        CHECK(winacp::encode(932, u"a\u4f60b", '?') == "a?b");
        CHECK(!winacp::encode(1252, u"\u3042"));
        // One replacement per code unit, as emitted by Windows: two for a character outside the
        // Basic Multilingual Plane, and one for an unpaired surrogate.
        CHECK(winacp::encode(932, u"a\U0001F600b", '?') == "a??b");
        CHECK(winacp::encode(932, u"a\xd83d"s + u"b", '?') == "a?b");
    }

    // Every decodable character encodes to a sequence that decodes to the same character. This
    // verifies the property on the embedded tables, independently of the Windows API.
    void everyPageGoesThereAndBack() {
        for (const int page : winacp::codePages()) {
            int checked = 0;
            for (int b = 0; b < 256; ++b) {
                for (int t = -1; t < 256; ++t) {
                    std::string bytes(1, char(b));
                    if (t >= 0) {
                        bytes += char(t);
                    }
                    const auto text = winacp::decode(page, bytes);
                    if (!text || text->size() != 1) {
                        continue;
                    }
                    const auto back = winacp::encode(page, *text);
                    CHECK(back.has_value());
                    if (back) {
                        CHECK(winacp::decode(page, *back) == text);
                    }
                    ++checked;
                }
            }
            CHECK(checked > 100);
        }
    }

}

int main() {
    everyPageIsHeld();
    theCharactersAreWhereWindowsHasThem();
    theUserDefinedRowsAreThere();
    aCharacterWithTwoSpellingsIsWrittenAsWindowsWritesIt();
    bytesThatDoNotDecodeAreRefused();
    whatAPageCannotHoldIsRefusedOrReplaced();
    everyPageGoesThereAndBack();
    return CHECK_RESULT();
}
