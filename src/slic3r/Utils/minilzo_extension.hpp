#ifndef MINILZO_EXTENSION_HPP
#define MINILZO_EXTENSION_HPP

#include <string>
#include <stdint.h>
#include "minilzo/minilzo.h"

namespace Slic3r {

// [INTENT] Public interface for LZO1X-1 compression and decompression routines.
// These are primarily used for compacting network message payloads.
//
// [UNITY] Replace with a managed C# LZO implementation (e.g., \`lzoutils\`)
// or a native plugin wrapper. Binary compatibility with the printer's LZO
// implementation is critical.
//
// [PORTING_HAZARD:P3] The underlying implementation (see minilzo_extension.cpp)
// uses a shared static buffer, making these calls thread-unsafe. A port should
// provide a thread-safe alternative.
//
int lzo_compress(unsigned char* in, uint64_t in_len, unsigned char* out, uint64_t* out_len);
int lzo_decompress(unsigned char* in, uint64_t in_len, unsigned char* out, uint64_t* out_len);

} // namespace Slic3r

#endif // MINIZ_EXTENSION_HPP
