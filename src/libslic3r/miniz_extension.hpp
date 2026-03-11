#ifndef MINIZ_EXTENSION_HPP
#define MINIZ_EXTENSION_HPP

#include <string>
#include <miniz.h>

namespace Slic3r {

// [INTENT] UTF-8 aware wrappers around miniz open/close routines keep archive IO
//          behavior consistent across platform-specific path encodings.
bool open_zip_reader(mz_zip_archive *zip, const std::string &fname_utf8);
bool open_zip_writer(mz_zip_archive *zip, const std::string &fname_utf8);
bool close_zip_reader(mz_zip_archive *zip);
bool close_zip_writer(mz_zip_archive *zip);

class MZ_Archive {
public:
    // [MEMORY] `mz_zip_archive` owns internal buffers/state managed by miniz init/end
    //          calls; this wrapper centralizes lifetime transitions in one object.
    mz_zip_archive arch;
    
    MZ_Archive();
    
    static std::string get_errorstr(mz_zip_error mz_err);
    
    std::string get_errorstr() const
    {
        // [HAZARD] Appends a literal "!" for UI-style emphasis; callers expecting raw
        //          miniz messages should use the static overload directly.
        return get_errorstr(arch.m_last_error) + "!";
    }

    bool is_alive() const
    {
        // [STATE] Uses miniz mode enum as a liveness signal to prevent operating on
        //         a writer after finalization.
        return arch.m_zip_mode != MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED;
    }
};

} // namespace Slic3r

#endif // MINIZ_EXTENSION_HPP
