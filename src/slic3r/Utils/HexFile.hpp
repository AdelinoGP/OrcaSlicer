#ifndef slic3r_Hex_hpp_
#define slic3r_Hex_hpp_

#include <string>
#include <boost/filesystem/path.hpp>

namespace Slic3r { namespace Utils {

/* [INTENT]
 * HexFile is a metadata extractor for Intel HEX firmware files.
 * It identifies the printer model/device type from a custom INI-style header
 * embedded in the HEX file comments.
 *
 * [STATE]
 * - path: Source file on disk.
 * - device: Normalized DeviceKind enum.
 * - model_id: Raw string identifier for the printer model.
 *
 * [UNITY]
 * Port as a pure C# class or struct. Use System.IO.File for reading and
 * System.Text.RegularExpressions or simple string splitting to parse the header.
 */
struct HexFile
{
    enum DeviceKind {
        DEV_GENERIC,
        DEV_MK2,
        DEV_MK3,
        DEV_MM_CONTROL,
        DEV_CW1,
        DEV_CW1S,
    };

    boost::filesystem::path path;
    DeviceKind              device = DEV_GENERIC;
    std::string             model_id;

    HexFile() {}
    HexFile(boost::filesystem::path path);
};

}} // namespace Slic3r::Utils

#endif
