#include "ThumbnailData.hpp"

namespace Slic3r {

void ThumbnailData::set(unsigned int w, unsigned int h)
{
    // [INTENT] Resize the RGBA backing store only when the requested thumbnail geometry changes.
    // Reusing the current buffer avoids wiping pixels during repeated export passes that render
    // the same preview size for multiple output formats.
    if ((w == 0) || (h == 0))
        return;

    if ((width != w) || (height != h)) {
        width  = w;
        height = h;
        // [STATE] reset-on-resize establishes the invariant used by encoders in Thumbnails.cpp:
        // a freshly sized thumbnail is fully initialized to opaque white before the renderer
        // overwrites pixels, so exporters never read uninitialized alpha/color bytes.
        // defaults to white texture
        pixels.clear();
        pixels = std::vector<unsigned char>(width * height * 4, 255);
    }
}

void ThumbnailData::reset()
{
    width  = 0;
    height = 0;
    pixels.clear();
}

bool ThumbnailData::is_valid() const
{
    // [HAZARD] width * height * 4 uses unsigned arithmetic from the file-format boundary. A port
    // should preserve the exact size check but guard against overflow explicitly if thumbnail
    // dimensions stop being constrained by UI/config validation.
    return (width != 0) && (height != 0) && ((unsigned int) pixels.size() == 4 * width * height);
}

} // namespace Slic3r
