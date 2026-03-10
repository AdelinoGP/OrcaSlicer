#ifndef slic3r_Format_3mf_hpp_
#define slic3r_Format_3mf_hpp_
#include <string>
#include <expat.h>

namespace Slic3r {
// [INTENT] Public API for OrcaSlicer's baseline 3MF dialect: ZIP container + core 3MF XML
//          geometry + PrusaSlicer metadata sidecars.
// [COUPLING] This is the shared foundation under `3mf.cpp`; Orca/Bambu round-trip extensions live in
//            `bbs_3mf.cpp` on top of the same Model / DynamicPrintConfig contract.
// [HAZARD] Both APIs mutate `Model` and config state in place rather than returning an immutable parse tree,
//          so call sites must treat import/export as stateful graph reconstruction.

// PrusaFileParser is used to check 3mf file is from Prusa
class PrusaFileParser
{
public:
    PrusaFileParser() {}
    ~PrusaFileParser() {}

    bool check_3mf_from_prusa(const std::string filename);
    void _start_element_handler(const char *name, const char **attributes);
    void _characters_handler(const XML_Char *s, int len);

private:
    const char *get_attribute_value_charptr(const char **attributes, unsigned int attributes_size, const char *attribute_key);
    std::string get_attribute_value_string(const char **attributes, unsigned int attributes_size, const char *attribute_key);

    static void XMLCALL start_element_handler(void *userData, const char *name, const char **attributes);
    static void XMLCALL characters_handler(void *userData, const XML_Char *s, int len);
private:
    // [STATE] Expat callbacks flip these flags while streaming the model XML to infer producer identity.
    //         The parser does not build a DOM; it only tracks enough metadata to detect PrusaSlicer output.
    bool       m_from_prusa         = false;
    bool       m_is_application_key = false;
    // [MEMORY] `XML_Parser` is an opaque expat handle owned by this wrapper for the duration of
    //          `check_3mf_from_prusa()`.
    XML_Parser m_parser;
};

    /* The format for saving the SLA points was changing in the past. This enum holds the latest version that is being currently used.
     * Examples of the Slic3r_PE_sla_support_points.txt for historically used versions:

     *  version 0 : object_id=1|-12.055421 -2.658771 10.000000
                    object_id=2|-14.051745 -3.570338 5.000000
        // no header and x,y,z positions of the points)

     * version 1 :  ThreeMF_support_points_version=1
                    object_id=1|-12.055421 -2.658771 10.000000 0.4 0.0
                    object_id=2|-14.051745 -3.570338 5.000000 0.6 1.0
        // introduced header with version number; x,y,z,head_size,is_new_island)
    */

    enum {
        support_points_format_version = 1
    };
    
    enum {
        drain_holes_format_version = 1
    };

    class Model;
    struct ConfigSubstitutionContext;
    class DynamicPrintConfig;
    struct ThumbnailData;

    // [INTENT] Load the core 3MF archive into the mutable OrcaSlicer scene graph.
    // [STATE] Populates `model`, merges embedded print settings into `config`, and records any option
    //         substitutions / migrations into `config_substitutions`.
    // [COUPLING] This is the base-reader counterpart to `bbs_3mf.cpp`; callers choose between them based on
    //            whether they need plain Prusa-compatible 3MF or Bambu/Orca round-trip fields.
    extern bool load_3mf(const char* path, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions, Model* model, bool check_version);

    // [INTENT] Serialize the current `Model` plus print config into the baseline Prusa-compatible 3MF package.
    // [STATE] May repair / normalize meshes during export, so `model` is intentionally non-const here.
    // [HAZARD] Standard 3MF export is not a full Orca round-trip format; Bambu-specific state is preserved by
    //          `bbs_3mf.cpp`, not this API.
    extern bool store_3mf(const char* path, Model* model, const DynamicPrintConfig* config, bool fullpath_sources, const ThumbnailData* thumbnail_data = nullptr, bool zip64 = true);

} // namespace Slic3r

#endif /* slic3r_Format_3mf_hpp_ */
