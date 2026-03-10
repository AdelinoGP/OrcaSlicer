#ifndef slic3r_Format_AMF_hpp_
#define slic3r_Format_AMF_hpp_

namespace Slic3r {

class Model;
class DynamicPrintConfig;
class ConfigSubstitutionContext;

// [INTENT] Public AMF import entry point. Parses XML or ZIP-wrapped AMF into the
//          core `Model` graph used by the rest of the slicer.
// [STATE] `config`, `config_substitutions`, and `use_inches` are optional output
//         channels; the parser mutates them in-place when AMF metadata provides values.
// [COUPLING] This loader writes directly into `Model` / `ModelObject` / `ModelVolume`
//            rather than returning a format-neutral intermediate representation.
// [HAZARD] AMF import remains supported for reading, but most legacy config and volume-type
//          metadata handling is disabled in `AMF.cpp`; downstream code must not assume full
//          round-trip fidelity with older Slic3r AMF exports.
// Load the content of an amf file into the given model and configuration.
extern bool load_amf(
    const char* path, DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions, Model* model, bool* use_inches);

// BBS: remove amf export
//  Save the given model and the config data into an amf file.
//  The model could be modified during the export process if meshes are not repaired or have no shared vertices
// extern bool store_amf(const char* path, Model* model, const DynamicPrintConfig* config, bool fullpath_sources);

} // namespace Slic3r

#endif /* slic3r_Format_AMF_hpp_ */
