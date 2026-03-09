// [INTENT] RasterBase.cpp — Encoders and factory function for SLA raster output.
//
// This file uses an unusual include guard pattern:
//   #ifndef SLARASTER_CPP / #define SLARASTER_CPP / ... / #endif
//
// [HAZARD] H916 — `#ifndef SLARASTER_CPP` wraps the ENTIRE .cpp file body.
//   This guard allows other TUs to `#include "RasterBase.cpp"` directly, which
//   AGGRaster.hpp does implicitly by depending on types defined here. This is a
//   deliberate but non-standard C++ pattern. The guard prevents double-inclusion
//   if both RasterBase.cpp and a TU that includes AGGRaster.hpp are compiled.
//   The risk: if the guard is ever removed or renamed, every TU that transitively
//   includes AGGRaster.hpp will get duplicate symbol link errors.
//
// [MEMORY] PNGRasterEncoder:
//   - Calls miniz `tdefl_write_image_to_png_file_in_memory` which internally mallocs
//     a raw buffer (rawdata). That buffer is then COPIED into a std::vector and freed
//     via MZ_FREE(rawdata). The copy is necessary; the buf lifetime is independent.
//   - On miniz error: returns an empty EncodedRaster. No exception thrown, no error log.
//     Callers must check `size() == 0` to detect failure.
//
// [COUPLING] `create_raster_grayscale_aa` creates concrete AGGRaster subtypes:
//   - gamma > 0 → RasterGrayscaleAAGammaPower (standard production path)
//   - gamma ≈ 1.0 → RasterGrayscaleAA with gamma_none (exact linear — rarely used)
//   - gamma <= 0 → RasterGrayscaleAA with gamma_threshold(0.5) (disables anti-aliasing)
//   The `gamma > 0` and `gamma ≈ 1.0` branches overlap: gamma=1.0 triggers the first
//   branch (>0) and never reaches the second. The `abs(gamma-1.) < 1e-6` branch is
//   effectively dead for the current call convention.
//
// [CONCURRENCY] Encoder functions are stateless; safe to call concurrently on separate inputs.

#ifndef SLARASTER_CPP
#define SLARASTER_CPP

#include <functional>

#include <libslic3r/SLA/RasterBase.hpp>
#include <libslic3r/SLA/AGGRaster.hpp>

// minz image write:
#include <miniz.h>

namespace Slic3r { namespace sla {

// [INTENT] PNG encoder: rasterizes the pixel buffer to PNG via miniz, returns EncodedRaster.
EncodedRaster PNGRasterEncoder::operator()(const void* ptr, size_t w, size_t h, size_t num_components)
{
    std::vector<uint8_t> buf;
    size_t               s = 0;

    void* rawdata = tdefl_write_image_to_png_file_in_memory(ptr, int(w), int(h), int(num_components), &s);

    // On error, data() will return an empty vector. No other info can be
    // retrieved from miniz anyway...
    if (rawdata == nullptr)
        return EncodedRaster({}, "png");

    auto pptr = static_cast<std::uint8_t*>(rawdata);

    buf.reserve(s);
    std::copy(pptr, pptr + s, std::back_inserter(buf));

    MZ_FREE(rawdata);
    return EncodedRaster(std::move(buf), "png");
}

std::ostream& operator<<(std::ostream& stream, const EncodedRaster& bytes)
{
    stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));

    return stream;
}

// [INTENT] PPM (P5 grayscale) encoder for debugging.
EncodedRaster PPMRasterEncoder::operator()(const void* ptr, size_t w, size_t h, size_t num_components)
{
    std::vector<uint8_t> buf;

    auto header = std::string("P5 ") + std::to_string(w) + " " + std::to_string(h) + " " + "255 ";

    auto   sz = w * h * num_components;
    size_t s  = sz + header.size();

    buf.reserve(s);

    auto buff = reinterpret_cast<const std::uint8_t*>(ptr);
    std::copy(header.begin(), header.end(), std::back_inserter(buf));
    std::copy(buff, buff + sz, std::back_inserter(buf));

    return EncodedRaster(std::move(buf), "ppm");
}

// [INTENT] Factory: dispatches to the correct AGGRaster variant based on gamma.
// [COUPLING] gamma ≈ 1.0 branch (abs(gamma-1.) < 1e-6) is dead: gamma > 0 is checked first.
std::unique_ptr<RasterBase> create_raster_grayscale_aa(const Resolution&        res,
                                                       const PixelDim&          pxdim,
                                                       double                   gamma,
                                                       const RasterBase::Trafo& tr)
{
    std::unique_ptr<RasterBase> rst;

    if (gamma > 0)
        rst = std::make_unique<RasterGrayscaleAAGammaPower>(res, pxdim, tr, gamma);
    else if (std::abs(gamma - 1.) < 1e-6)
        rst = std::make_unique<RasterGrayscaleAA>(res, pxdim, tr, agg::gamma_none());
    else
        rst = std::make_unique<RasterGrayscaleAA>(res, pxdim, tr, agg::gamma_threshold(.5));

    return rst;
}

}} // namespace Slic3r::sla

#endif // SLARASTER_CPP
