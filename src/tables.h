#ifndef WINACP_TABLES_H
#define WINACP_TABLES_H

#include <cstddef>

// The contents of tables/, embedded into the library by cmake/embed.cmake.
namespace winacp::detail {

    struct Blob {
        int codePage;
        const unsigned char *data;
        std::size_t size;
    };

    /// Sorted in ascending order of code page.
    extern const Blob blobs[];
    extern const std::size_t blobCount;

}

#endif // WINACP_TABLES_H
