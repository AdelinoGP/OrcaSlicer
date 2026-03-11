#ifndef slic3r_GCodeThumbnails_hpp_
#define slic3r_GCodeThumbnails_hpp_

#include "../Point.hpp"
#include "../PrintConfig.hpp"
#include "../enum_bitmask.hpp"
#include "ThumbnailData.hpp"
#include "../enum_bitmask.hpp"

#include <vector>
#include <memory>
#include <string_view>

#include <boost/beast/core/detail/base64.hpp>

namespace Slic3r {
enum class ThumbnailError : int { InvalidVal, OutOfRange, InvalidExt };
using ThumbnailErrors = enum_bitmask<ThumbnailError>;
ENABLE_ENUM_BITMASK_OPERATORS(ThumbnailError);
} // namespace Slic3r

namespace Slic3r::GCodeThumbnails {

// [INTENT] Thumbnails.hpp is the public boundary between preview rendering and G-code export.
// Callers provide a thumbnail generator callback plus a list of requested sizes/formats; this
// module compresses the resulting RGBA buffers and emits the firmware-specific comment blocks.
//
// [STATE] export_thumbnails_to_file() is logically a streaming serializer. It mutates only local
// loop state (output order, ColPic first/secondary markers) while delegating all persistent render
// state to thumbnail_cb and all persistent writer state to the supplied output functor.
//
// [MEMORY] CompressedImageBuffer is an owning polymorphic wrapper around C-library allocations.
// Each format implementation chooses its own allocator/free pair, so callers must destroy through
// the virtual base to release memory correctly.
//
// [COUPLING] Depends on PrintConfig's GCodeThumbnailsFormat enum, ThumbnailData/ThumbnailsParams,
// and boost::beast base64 internals. This makes the interface small, but ties ports to the same
// "render first, comment-encode later" pipeline shape.
//
// [HAZARD] The base64 helper comes from Boost's detail namespace, which is not a stable public API.
// A port should treat the requirement as generic RFC 4648 encoding, not as a need to mirror Boost.

struct CompressedImageBuffer
{
    void*  data{nullptr};
    size_t size{0};
    virtual ~CompressedImageBuffer() {}
    virtual std::string_view tag() const = 0;
};

std::string                            get_hex(const unsigned int input);
std::string                            rjust(std::string input, unsigned int width, char fill_char);
std::unique_ptr<CompressedImageBuffer> compress_thumbnail(const ThumbnailData& data, GCodeThumbnailsFormat format);
std::string                            get_error_string(const ThumbnailErrors& errors);

typedef std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>> GCodeThumbnailDefinitionsList;
using namespace std::literals;
std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const std::string&     thumbnails_string,
                                                                                        const std::string_view def_ext = "PNG"sv);
std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const ConfigBase& config);

template<typename WriteToOutput, typename ThrowIfCanceledCallback>
inline void export_thumbnails_to_file(ThumbnailsGeneratorCallback&                                thumbnail_cb,
                                      int                                                         plate_id,
                                      const std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>>& thumbnails_list,
                                      WriteToOutput                                               output,
                                      ThrowIfCanceledCallback                                     throw_if_canceled)
{
    // [INTENT] Serialize one or more rendered thumbnails directly into the G-code stream without
    // staging a full file-sized buffer. Each format gets its firmware-specific wrapper, but the
    // outer traversal order stays stable so printers/UI parsers can predict where previews appear.
    //
    // [COUPLING] thumbnail_cb typically comes from GUI/preview code and must honor the plate_id,
    // printable_only, and show_bed flags encoded here. Changing those defaults changes both the
    // look of the embedded preview and the metadata some printer frontends infer from it.
    // Write thumbnails using base64 encoding
    if (thumbnail_cb == nullptr)
        return;
    short i            = 0;
    bool  first_ColPic = true;
    for (const auto& [format, size] : thumbnails_list) {
        static constexpr const size_t max_row_length = 78;
        ThumbnailsList                thumbnails     = thumbnail_cb(ThumbnailsParams{{size}, true, true, true, true, plate_id});
        for (const ThumbnailData& data : thumbnails) {
            if (data.is_valid()) {
                auto compressed = compress_thumbnail(data, format);
                if (compressed->data && compressed->size) {
                    // [HAZARD] Output syntax is firmware-specific and parser-sensitive:
                    //  - BTT_TFT expects raw RGB565 lines with CRLF terminators.
                    //  - ColPic uses gimage/simage markers instead of THUMBNAIL_BLOCK_* sentinels.
                    //  - PNG/JPG/QOI rely on exact begin/end tags plus 78-char base64 wrapping.
                    // A translator should preserve these wire formats byte-for-byte or make them
                    // explicit formatter backends instead of folding them into one generic encoder.
                    if (format == GCodeThumbnailsFormat::BTT_TFT) {
                        // write BTT_TFT header
                        output((";" + rjust(get_hex(data.width), 4, '0') + rjust(get_hex(data.height), 4, '0') + "\r\n").c_str());
                        output((char*) compressed->data);
                        if (i == (thumbnails_list.size() - 1))
                            output("; bigtree thumbnail end\r\n\r\n");
                    } else if (format == GCodeThumbnailsFormat::ColPic) {
                        if (first_ColPic) {
                            output((boost::format("\n\n;gimage:%s\n\n") % reinterpret_cast<char*>(compressed->data)).str().c_str());
                        } else {
                            output((boost::format("\n\n;simage:%s\n\n") % reinterpret_cast<char*>(compressed->data)).str().c_str());
                        }
                        first_ColPic = false;
                    } else {
                        // [INTENT] Standard thumbnail blocks are emitted as comment-only payloads
                        // so the preview survives on firmware that ignores unknown comments while
                        // remaining easy for host software to locate via THUMBNAIL_BLOCK markers.
                        output("; THUMBNAIL_BLOCK_START\n");
                        std::string encoded;
                        encoded.resize(boost::beast::detail::base64::encoded_size(compressed->size));
                        encoded.resize(
                            boost::beast::detail::base64::encode((void*) encoded.data(), (const void*) compressed->data, compressed->size));
                        output((boost::format("\n;\n; %s begin %dx%d %d\n") % compressed->tag() % data.width % data.height % encoded.size())
                                   .str()
                                   .c_str());
                        while (encoded.size() > max_row_length) {
                            output((boost::format("; %s\n") % encoded.substr(0, max_row_length)).str().c_str());
                            encoded = encoded.substr(max_row_length);
                        }

                        // Orca write remaining ecoded data
                        if (encoded.size() > 0)
                            output((boost::format("; %s\n") % encoded).str().c_str());

                        output((boost::format("; %s end\n") % compressed->tag()).str().c_str());
                        output("; THUMBNAIL_BLOCK_END\n\n");
                    }
                    // [CONCURRENCY] Cancellation is polled after each thumbnail payload is flushed,
                    // not during compression. Long-running encoders therefore remain cooperative but
                    // not preemptible inside a single format conversion.
                    throw_if_canceled();
                }
            }
        }
        i++;
    }
}

} // namespace Slic3r::GCodeThumbnails

#endif // slic3r_GCodeThumbnails_hpp_
