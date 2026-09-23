#ifndef WINACP_TABLES_H
#define WINACP_TABLES_H

#include <cstddef>
#include <cstdint>

// The contents of tables/, compiled into arrays by tools/embed.
namespace winacp::detail {

    /// The table of one code page. The value 0xFFFF denotes an invalid sequence.
    struct TableData {
        int codePage;

        /// The decoding of each single byte. 0xFFFF at a lead byte.
        const std::uint16_t *single;

        /// One more than the row index of each lead byte in \c rows , and 0 for any other byte.
        const std::uint8_t *rowOf;

        /// The decoding of each two-byte sequence, 256 code units per lead byte in ascending
        /// order of lead byte. Null if the code page has no lead bytes.
        const std::uint16_t *rows;

        /// Pairs of a code unit and the sequence Windows encodes it to, packed as
        /// (lead << 8) | trail, for each code unit that Windows encodes to a sequence other than
        /// the first in byte order that decodes to it. Null if there are none.
        const std::uint16_t (*overrides)[2];
        std::size_t overrideCount;
    };

    /// Sorted in ascending order of code page.
    extern const TableData tables[];
    extern const std::size_t tableCount;

}

#endif // WINACP_TABLES_H
