// PNP fork: implementation of the 3MF project_settings.config sidecar merge.
// See PnpModelSidecar.hpp for the contract.

#include "PnpModelSidecar.hpp"

#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <miniz.h>

#include <nlohmann/json.hpp>

namespace Slic3r {
namespace GUI {

namespace {

// The 3MF sidecar file pnp_cli reads its config from (bbs_3mf.cpp
// BBS_PROJECT_CONFIG_FILE). Matched case-insensitively, like pnp's own
// `read_3mf_project_settings` (loader.rs: `ends_with("project_settings.config")`).
const char* const PROJECT_SETTINGS_CONFIG_ENTRY = "Metadata/project_settings.config";

} // anonymous namespace

bool merge_translated_config_into_3mf(const boost::filesystem::path& path, const nlohmann::json& translated)
{
    mz_zip_archive reader;
    memset(&reader, 0, sizeof(reader));
    if (!mz_zip_reader_init_file(&reader, path.string().c_str(), 0)) {
        BOOST_LOG_TRIVIAL(error) << "merge_translated_config_into_3mf: cannot open " << path.string();
        return false;
    }

    const mz_uint num_entries = mz_zip_reader_get_num_files(&reader);
    mz_uint      sidecar_index = 0;
    bool         found         = false;
    for (mz_uint i = 0; i < num_entries; ++i) {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&reader, i, &stat) && stat.m_filename != nullptr &&
            boost::iequals(stat.m_filename, PROJECT_SETTINGS_CONFIG_ENTRY)) {
            sidecar_index = i;
            found         = true;
            break;
        }
    }
    if (!found) {
        BOOST_LOG_TRIVIAL(error) << "merge_translated_config_into_3mf: " << PROJECT_SETTINGS_CONFIG_ENTRY
                                 << " missing from " << path.string();
        mz_zip_reader_end(&reader);
        return false;
    }

    mz_zip_archive_file_stat sidecar_stat;
    if (!mz_zip_reader_file_stat(&reader, sidecar_index, &sidecar_stat)) {
        mz_zip_reader_end(&reader);
        return false;
    }
    std::string sidecar_text(sidecar_stat.m_uncomp_size, '\0');
    if (!mz_zip_reader_extract_to_mem(&reader, sidecar_index, sidecar_text.data(), sidecar_text.size(), 0)) {
        mz_zip_reader_end(&reader);
        return false;
    }

    nlohmann::json sidecar = nlohmann::json::parse(sidecar_text, nullptr, /*allow_exceptions=*/false);
    if (sidecar.is_discarded() || !sidecar.is_object()) {
        mz_zip_reader_end(&reader);
        return false;
    }
    for (const auto& [key, value] : translated.items())
        sidecar[key] = value;
    const std::string merged = sidecar.dump(1, '\t');

    // Rewrite the archive: copy every entry except the sidecar, then add the
    // merged sidecar. miniz cannot replace an entry in place.
    const boost::filesystem::path tmp_path = path.string() + ".pnp.tmp";
    mz_zip_archive writer;
    memset(&writer, 0, sizeof(writer));
    if (!mz_zip_writer_init_file(&writer, tmp_path.string().c_str(), 0)) {
        mz_zip_reader_end(&reader);
        return false;
    }
    bool ok = true;
    for (mz_uint i = 0; ok && i < num_entries; ++i) {
        if (i == sidecar_index)
            continue;
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&reader, i, &stat) || stat.m_filename == nullptr) {
            ok = false;
            break;
        }
        std::vector<char> data(stat.m_uncomp_size);
        if (!mz_zip_reader_extract_to_mem(&reader, i, data.data(), data.size(), 0) ||
            !mz_zip_writer_add_mem(&writer, stat.m_filename, data.data(), data.size(), MZ_DEFAULT_COMPRESSION)) {
            ok = false;
            break;
        }
    }
    if (ok && !mz_zip_writer_add_mem(&writer, PROJECT_SETTINGS_CONFIG_ENTRY, merged.data(), merged.size(),
                                     MZ_DEFAULT_COMPRESSION))
        ok = false;
    if (ok && !mz_zip_writer_finalize_archive(&writer))
        ok = false;
    mz_zip_writer_end(&writer);
    mz_zip_reader_end(&reader);
    if (!ok) {
        boost::system::error_code ec;
        boost::filesystem::remove(tmp_path, ec);
        return false;
    }

    boost::system::error_code ec;
    boost::filesystem::remove(path, ec);
    boost::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "merge_translated_config_into_3mf: cannot replace " << path.string()
                                 << ": " << ec.message();
        return false;
    }
    return true;
}

} // namespace GUI
} // namespace Slic3r
