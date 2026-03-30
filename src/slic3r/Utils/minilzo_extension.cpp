// [ANNOTATED]
#include <exception>
#include <stdio.h>
#include <stdlib.h>
#include "minilzo_extension.hpp"
#include "minilzo/minilzo.h"

// [STATE] Static work memory for LZO compression.
// [THREAD] This shared buffer makes `lzo_compress` thread-unsafe.
static unsigned char wrkmem[LZO1X_1_MEM_COMPRESS];

static bool initialized = false;

namespace Slic3r {

// [INTENT] Provides a simplified C++ wrapper around the minilzo library for
// LZO1X compression and decompression, typically used for network message payloads.
//
// [UNITY] Use a managed C# LZO implementation (e.g., from a NuGet package like `lzoutils`)
// or a native plugin wrapper. Ensure binary compatibility with the printer-side
// LZO1X-1 implementation.
//
// [PORTING_HAZARD:P3] The current implementation is thread-unsafe due to the
// shared `wrkmem` buffer. Unity ports should use per-instance or per-thread buffers
// if concurrent compression is required.
//
int lzo_compress(unsigned char* in, uint64_t in_len, unsigned char* out, uint64_t* out_len)
{
    int result = 0;
    if (!initialized) {
        if (lzo_init() != LZO_E_OK)
            return LZO_E_ERROR;
        else
            initialized = true;
    }

    lzo_uint lzo_out_len = *out_len;
    result               = lzo1x_1_compress(in, in_len, out, &lzo_out_len, wrkmem);
    if (result == LZO_E_OK) {
        *out_len = lzo_out_len;
        return 0;
    }

    return result;
}

int lzo_decompress(unsigned char* in, uint64_t in_len, unsigned char* out, uint64_t* out_len)
{
    int result = 0;
    if (!initialized) {
        if (lzo_init() != LZO_E_OK)
            return LZO_E_ERROR;
        else
            initialized = true;
    }

    lzo_uint lzo_out_len = *out_len;
    result               = lzo1x_decompress(in, in_len, out, &lzo_out_len, NULL);
    if (result == LZO_E_OK) {
        *out_len = lzo_out_len;
        return 0;
    }

    return result;
}

} // namespace Slic3r
