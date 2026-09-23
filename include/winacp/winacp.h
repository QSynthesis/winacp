#ifndef WINACP_WINACP_H
#define WINACP_WINACP_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <winacp/winacp_export.h>

/// Conversion between UTF-16 and the Windows ANSI code pages, identical to that of Windows on
/// every platform.
///
/// On Windows, a program that writes text in the system encoding uses the ANSI code page of the
/// host machine, and the resulting file records no encoding. Reading such a file on another
/// system, and writing it back in a form that the originating program reads identically,
/// requires the exact mapping that Windows applies. ICU approximates this mapping but is not
/// available on every platform, and the native macOS converters omit the Private Use Area
/// mappings and encode certain characters differently.
///
/// The mapping is therefore captured from Windows by tools/generate and embedded as tables.
namespace winacp {

    /// A Windows ANSI code page. Each enumerator equals the code page number that Windows uses.
    ///
    /// The enumerators are named after the script or language of the code page rather than after
    /// an encoding, because encoding names are ambiguous: "Shift_JIS" denotes code page 932 in
    /// some programs and JIS X 0208 in others. The corresponding encoding name is given in the
    /// comment of each enumerator that has a common one.
    enum class CodePage : int {
        Thai = 874,
        Japanese = 932,           ///< Microsoft variant of Shift_JIS
        SimplifiedChinese = 936,  ///< GBK
        Korean = 949,             ///< Unified Hangul Code
        TraditionalChinese = 950, ///< Microsoft variant of Big5
        CentralEuropean = 1250,
        Cyrillic = 1251,
        WesternEuropean = 1252, ///< Latin 1
        Greek = 1253,
        Turkish = 1254,
        Hebrew = 1255,
        Arabic = 1256,
        Baltic = 1257,
        Vietnamese = 1258,
    };

    /// Returns the code page number of \a codePage , for a file format or an operating system
    /// interface that records the number.
    constexpr int toNumber(CodePage codePage) {
        return static_cast<int>(codePage);
    }

    /// Returns the code page of \a number , or \c std::nullopt if no
    /// supported code page has that number.
    ///
    /// A code page number is commonly obtained from data rather than written in the source: the
    /// ANSI code page of the host, an encoding recorded in a configuration file, or a selection
    /// made by the user. This function is where such a number is validated.
    WINACP_EXPORT std::optional<CodePage> codePageFromNumber(int number);

    /// Returns the supported code pages in ascending order of number: all code pages that
    /// Windows uses as the ANSI code page of a locale.
    WINACP_EXPORT const std::vector<CodePage> &codePages();

    /// Decodes \a bytes from \a codePage .
    ///
    /// \return the decoded text, or \c std::nullopt if the code page is not supported or if any
    ///         byte sequence is invalid, consistent with MultiByteToWideChar under
    ///         MB_ERR_INVALID_CHARS. An invalid sequence indicates that the wrong code page was
    ///         selected, and substituting replacement characters would conceal that error.
    WINACP_EXPORT std::optional<std::u16string> decode(CodePage codePage, std::string_view bytes);

    /// Encodes \a text into \a codePage .
    ///
    /// A character with more than one encoding, such as a character of code page 932 with both
    /// an NEC and an IBM encoding, is encoded to the sequence that Windows produces. A small
    /// number of characters to which no sequence decodes are encoded nevertheless, because
    /// Windows encodes them; decoding the result therefore does not always reproduce \a text .
    ///
    /// \return the encoded bytes, or \c std::nullopt if the code page is not supported or if
    ///         any character cannot be represented in it
    WINACP_EXPORT std::optional<std::string> encode(CodePage codePage, std::u16string_view text);

    /// \overload
    ///
    /// Emits \a replacement for each UTF-16 code unit that cannot be represented, and therefore
    /// two for a character outside the Basic Multilingual Plane. This matches the default
    /// character of WideCharToMultiByte, which is a question mark on every supported code page.
    ///
    /// \return the encoded bytes, or an empty string if the code page is not supported
    WINACP_EXPORT std::string encode(CodePage codePage, std::u16string_view text, char replacement);

}

#endif // WINACP_WINACP_H
