// Platform-independent tests against fixed expected values.
//
// test_against_windows compares the tables with the Windows API. This test runs on all
// platforms and verifies that the embedded tables are intact where that API is unavailable.
#include <cstddef>
#include <string>

#include <winacp/winacp.h>

#include "check.h"

using namespace std::string_literals;

using CP = winacp::CodePage;

namespace {

    bool decodes(CP page, const std::string &bytes, const std::u16string &text) {
        const auto got = winacp::decode(page, bytes);
        return got && *got == text;
    }

    bool encodes(CP page, const std::u16string &text, const std::string &bytes) {
        const auto got = winacp::encode(page, text);
        return got && *got == bytes;
    }

    void everyPageIsHeld() {
        const int expected[] = {874,  932,  936,  949,  950,  1250, 1251,
                                1252, 1253, 1254, 1255, 1256, 1257, 1258};
        CHECK(winacp::codePages().size() == sizeof(expected) / sizeof(expected[0]));
        for (std::size_t i = 0; i < winacp::codePages().size(); ++i) {
            CHECK(winacp::toNumber(winacp::codePages()[i]) == expected[i]);
            CHECK(winacp::codePageFromNumber(expected[i]) == winacp::codePages()[i]);
        }

        // Numbers of code pages that Windows does not use as an ANSI code page.
        CHECK(!winacp::codePageFromNumber(65001)); // UTF-8
        CHECK(!winacp::codePageFromNumber(54936)); // GB18030
        CHECK(!winacp::codePageFromNumber(0));

        // A value formed by a cast rather than by codePageFromNumber() is refused by every
        // conversion function.
        const auto unsupported = static_cast<CP>(65001);
        CHECK(!winacp::decode(unsupported, "a"));
        CHECK(!winacp::encode(unsupported, u"a"));
        CHECK(winacp::encode(unsupported, u"a", '?').empty());
    }

    // The enumerators are the numbers Windows uses.
    void theEnumeratorsAreTheCodePageNumbers() {
        CHECK(winacp::toNumber(CP::Thai) == 874);
        CHECK(winacp::toNumber(CP::Japanese) == 932);
        CHECK(winacp::toNumber(CP::SimplifiedChinese) == 936);
        CHECK(winacp::toNumber(CP::Korean) == 949);
        CHECK(winacp::toNumber(CP::TraditionalChinese) == 950);
        CHECK(winacp::toNumber(CP::CentralEuropean) == 1250);
        CHECK(winacp::toNumber(CP::Cyrillic) == 1251);
        CHECK(winacp::toNumber(CP::WesternEuropean) == 1252);
        CHECK(winacp::toNumber(CP::Greek) == 1253);
        CHECK(winacp::toNumber(CP::Turkish) == 1254);
        CHECK(winacp::toNumber(CP::Hebrew) == 1255);
        CHECK(winacp::toNumber(CP::Arabic) == 1256);
        CHECK(winacp::toNumber(CP::Baltic) == 1257);
        CHECK(winacp::toNumber(CP::Vietnamese) == 1258);
    }

    // One representative character per code page, as converted by Windows.
    void theCharactersAreWhereWindowsHasThem() {
        CHECK(decodes(CP::Japanese, "\x82\xa0"s, u"\u3042")); // あ
        CHECK(encodes(CP::Japanese, u"\u3042", "\x82\xa0"s));
        CHECK(decodes(CP::SimplifiedChinese, "\xc4\xe3"s, u"\u4f60")); // 你
        CHECK(decodes(CP::SimplifiedChinese, "\x80"s, u"\u20ac")); // €, absent from standard GBK
        CHECK(decodes(CP::Korean, "\xc7\xd1"s, u"\ud55c"));        // 한
        CHECK(decodes(CP::TraditionalChinese, "\xa4\xa4"s, u"\u4e2d")); // 中
        CHECK(decodes(CP::Cyrillic, "\xc0"s, u"\u0410"));               // А
        CHECK(decodes(CP::WesternEuropean, "\x80"s, u"\u20ac"));        // €
        CHECK(decodes(CP::Thai, "\xa1"s, u"\u0e01"));                   // ก
        CHECK(decodes(CP::Japanese, "\\~"s, u"\\~")); // ASCII, not YEN SIGN or OVERLINE
    }

    // User-defined rows, which Windows maps into the Private Use Area. ICU converts these
    // incorrectly on code page 932, and the macOS converters omit them.
    void theUserDefinedRowsAreThere() {
        CHECK(decodes(CP::Japanese, "\xf0\x40"s, u"\ue000"));
        CHECK(decodes(CP::SimplifiedChinese, "\xaa\xa1"s, u"\ue000"));
        CHECK(decodes(CP::TraditionalChinese, "\xfa\x40"s, u"\ue000"));
        CHECK(encodes(CP::TraditionalChinese, u"\ue000", "\xfa\x40"s));
    }

    // A character with both an NEC and an IBM encoding is encoded as by Windows, regardless of
    // the sequence it was decoded from.
    void aCharacterWithTwoSpellingsIsWrittenAsWindowsWritesIt() {
        CHECK(decodes(CP::Japanese, "\xed\x40"s, u"\u7e8a"));
        CHECK(decodes(CP::Japanese, "\xfa\x5c"s, u"\u7e8a"));
        CHECK(encodes(CP::Japanese, u"\u7e8a", "\xfa\x5c"s));
        CHECK(decodes(CP::Japanese, "\x87\x90"s, u"\u2252")); // NEC row 13; encoded to JIS row 2
        CHECK(encodes(CP::Japanese, u"\u2252", "\x81\xe0"s));
    }

    // Invalid sequences are rejected rather than replaced.
    void bytesThatDoNotDecodeAreRefused() {
        CHECK(!winacp::decode(CP::Japanese, "\x82"s)); // truncated sequence
        CHECK(!winacp::decode(CP::Japanese, "a\x82"s));
        CHECK(!winacp::decode(CP::Japanese, "\x82\x20"s)); // invalid trail byte
        CHECK(!winacp::decode(CP::Japanese, "\xa0"s));
        CHECK(!winacp::decode(CP::SimplifiedChinese, "\xff"s));
        CHECK(decodes(CP::Japanese, ""s, u""));
    }

    void whatAPageCannotHoldIsRefusedOrReplaced() {
        CHECK(!winacp::encode(CP::Japanese, u"\u4f60")); // 你, absent from code page 932
        CHECK(winacp::encode(CP::Japanese, u"a\u4f60b", '?') == "a?b");
        CHECK(!winacp::encode(CP::WesternEuropean, u"\u3042"));
        // One replacement per code unit, as emitted by Windows: two for a character outside the
        // Basic Multilingual Plane, and one for an unpaired surrogate.
        CHECK(winacp::encode(CP::Japanese, u"a\U0001F600b", '?') == "a??b");
        CHECK(winacp::encode(CP::Japanese, u"a\xd83d"s + u"b", '?') == "a?b");
    }

    // Every decodable character encodes to a sequence that decodes to the same character. This
    // verifies the property on the embedded tables, independently of the Windows API.
    void everyPageGoesThereAndBack() {
        for (const CP page : winacp::codePages()) {
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
    theEnumeratorsAreTheCodePageNumbers();
    theCharactersAreWhereWindowsHasThem();
    theUserDefinedRowsAreThere();
    aCharacterWithTwoSpellingsIsWrittenAsWindowsWritesIt();
    bytesThatDoNotDecodeAreRefused();
    whatAPageCannotHoldIsRefusedOrReplaced();
    everyPageGoesThereAndBack();
    return CHECK_RESULT();
}
