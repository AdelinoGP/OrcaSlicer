#include "HexFile.hpp"

#include <sstream>
#include <boost/filesystem/fstream.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;

namespace Slic3r { namespace Utils {

static HexFile::DeviceKind parse_device_kind(const std::string& str)
{
    if (str == "mk2") {
        return HexFile::DEV_MK2;
    } else if (str == "mk3") {
        return HexFile::DEV_MK3;
    } else if (str == "mm-control") {
        return HexFile::DEV_MM_CONTROL;
    } else if (str == "cw1") {
        return HexFile::DEV_CW1;
    } else if (str == "cw1s") {
        return HexFile::DEV_CW1S;
    } else {
        return HexFile::DEV_GENERIC;
    }
}

/* [INTENT]
 * Scans the hex file for terminator sequences to guess the device type
 * when metadata is missing.
 */
static size_t hex_num_sections(fs::ifstream& file)
{
    file.seekg(0);
    if (!file.good()) {
        return 0;
    }

    static const char* hex_terminator = ":00000001FF\r";
    size_t             res            = 0;
    std::string        line;
    while (getline(file, line, '\n').good()) {
        // Account for LF vs CRLF
        if (!line.empty() && line.back() != '\r') {
            line.push_back('\r');
        }

        if (line == hex_terminator) {
            res++;
        }
    }

    return res;
}

/* [INTENT]
 * Constructor that performs the actual parsing.
 * It reads the file line by line, collecting comment lines (starting with ';')
 * until it hits the first hex record (starting with ':').
 * The collected comments are then parsed as an INI property tree.
 *
 * [STATE]
 * Mutates the HexFile object's device and model_id based on parsing results.
 *
 * [THREAD]
 * Synchronous file I/O. Should be called from a worker thread if the file is large,
 * although firmware headers are usually at the very beginning.
 */
HexFile::HexFile(fs::path path) : path(std::move(path))
{
    fs::ifstream file(this->path);
    if (!file.good()) {
        return;
    }

    std::string       line;
    std::stringstream header_ini;
    while (std::getline(file, line, '\n').good()) {
        if (line.empty()) {
            continue;
        }

        // Account for LF vs CRLF
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.front() == ';') {
            line.front() = ' ';
            header_ini << line << std::endl;
        } else if (line.front() == ':') {
            break;
        }
    }

    pt::ptree ptree;
    try {
        pt::read_ini(header_ini, ptree);
    } catch (std::exception& /* e */) {
        return;
    }

    bool       has_device_meta = false;
    const auto device          = ptree.find("device");
    if (device != ptree.not_found()) {
        this->device    = parse_device_kind(device->second.data());
        has_device_meta = true;
    }

    const auto model_id = ptree.find("model_id");
    if (model_id != ptree.not_found()) {
        this->model_id = model_id->second.data();
    }

    if (!has_device_meta) {
        // No device metadata, look at the number of 'sections'
        if (hex_num_sections(file) == 2) {
            // Looks like a pre-metadata l10n firmware for the MK3, assume that's the case
            this->device = DEV_MK3;
        }
    }
}

}} // namespace Slic3r::Utils
