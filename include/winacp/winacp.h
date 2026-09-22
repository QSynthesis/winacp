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
///
/// Code pages are identified by number. The mapping from encoding names to numbers is left to
/// the caller, because names are ambiguous: "Shift_JIS" denotes code page 932 in some programs
/// and JIS X 0208 in others.
namespace winacp {

    /// Returns the supported code pages in ascending order: all code pages that Windows uses as
    /// the ANSI code page of a locale.
    WINACP_EXPORT const std::vector<int> &codePages();

    /// Returns whether \a codePage is supported.
    WINACP_EXPORT bool isAvailable(int codePage);

    /// Decodes \a bytes from \a codePage .
    ///
    /// \return the decoded text, or \c std::nullopt if the code page is not supported or if any
    ///         byte sequence is invalid, consistent with MultiByteToWideChar under
    ///         MB_ERR_INVALID_CHARS. An invalid sequence indicates that the wrong code page was
    ///         selected, and substituting replacement characters would conceal that error.
    WINACP_EXPORT std::optional<std::u16string> decode(int codePage, std::string_view bytes);

    /// Encodes \a text into \a codePage .
    ///
    /// A character with more than one encoding, such as a character of code page 932 with both
    /// an NEC and an IBM encoding, is encoded to the sequence that Windows produces. A small
    /// number of characters to which no sequence decodes are encoded nevertheless, because
    /// Windows encodes them; decoding the result therefore does not always reproduce \a text .
    ///
    /// \return the encoded bytes, or \c std::nullopt if the code page is not supported or if
    ///         any character cannot be represented in it
    WINACP_EXPORT std::optional<std::string> encode(int codePage, std::u16string_view text);

    /// \overload
    ///
    /// Emits \a replacement for each UTF-16 code unit that cannot be represented, and therefore
    /// two for a character outside the Basic Multilingual Plane. This matches the default
    /// character of WideCharToMultiByte, which is a question mark on every supported code page.
    ///
    /// \return the encoded bytes, or an empty string if the code page is not supported
    WINACP_EXPORT std::string encode(int codePage, std::u16string_view text, char replacement);

}

#endif // WINACP_WINACP_H
