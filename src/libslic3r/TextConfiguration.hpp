#ifndef slic3r_TextConfiguration_hpp_
#define slic3r_TextConfiguration_hpp_

#include <vector>
#include <string>
#include <optional>
#include <cereal/cereal.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cereal/archives/binary.hpp>
#include "Point.hpp" // Transform3d

namespace Slic3r {

// [INTENT] Configuration structures for embossed text functionality.
// Stores font properties, style definitions, and text content for 3D text volumes.
// Used by ModelVolume to persist text that can be embossed onto 3D models.
// [COUPLING] Uses cereal for serialization (3MF export/import), depends on Point.hpp
// for Transform3d. Tightly coupled to wxWidgets for font descriptors on GUI side.
// [HAZARD] Platform-dependent font descriptors (wx_win_font_descr, wx_lin_font_descr)
// create cross-platform portability issues for text styles.
struct FontProp
{
    // [INTENT] Character spacing adjustment - extra space between letters.
    // Negative values bring letters closer, positive spreads them.
    // [STATE] Optional type - when not set, defaults to zero and is not serialized.
    // [MEMORY] std::optional manages heap allocation for the integer only when set.
    std::optional<int> char_gap; // [in font point]

    // [INTENT] Line spacing adjustment - extra space between text lines.
    // Negative values bring lines closer, positive spreads them.
    std::optional<int> line_gap; // [in font point]

    // [INTENT] Width modifier - positive makes characters wider, negative makes them narrower.
    std::optional<float> boldness; // [in mm]

    // [INTENT] Skew/italic angle - positive skews right (CW), negative skews left.
    std::optional<float> skew; // [ration x:y]

    // [INTENT] Font collection index - for TrueType Font collections (TTC files)
    // selecting which font in the collection to use.
    std::optional<unsigned int> collection_number;

    // [INTENT] Per-glyph transformation flag - enables individual glyph positioning
    // for advanced text effects vs uniform transformation for entire string.
    bool per_glyph;

    // [INTENT] Text alignment - horizontal and vertical positioning relative to origin.
    // [HAZARD] Uses pair<> for alignment - not type-safe, could use struct instead.
    enum class HorizontalAlign { left = 0, center, right };
    enum class VerticalAlign { top = 0, center, bottom };
    using Align = std::pair<HorizontalAlign, VerticalAlign>;
    Align align = Align(HorizontalAlign::center, VerticalAlign::center);

    //////
    // Duplicit data to wxFontDescriptor
    // used for store/load .3mf file
    //////

    // Height of text line (letters)
    // duplicit to wxFont::PointSize
    // [STATE] Stored in mm for print space - converted to font points for rendering.
    float size_in_mm; // [in mm]

    // [INTENT] Font family metadata - used for font substitution when the original
    // font is not available on the system. Stored in 3MF for reproducibility.
    std::optional<std::string> family;
    std::optional<std::string> face_name;
    std::optional<std::string> style;
    std::optional<std::string> weight;

    /// <summary>
    /// Only constructor with restricted values
    /// </summary>
    /// <param name="line_height">Y size of text [in mm]</param>
    /// <param name="depth">Z size of text [in mm]</param>
    FontProp(float line_height = 10.f) : size_in_mm(line_height), per_glyph(false) {}

    bool operator==(const FontProp& other) const
    {
        return char_gap == other.char_gap && line_gap == other.line_gap && per_glyph == other.per_glyph && align == other.align &&
               is_approx(size_in_mm, other.size_in_mm) && is_approx(boldness, other.boldness) && is_approx(skew, other.skew);
    }

    // [MEMORY] Cereal serialization - templated Archive parameter allows both
    // binary (for .3mf) and JSON/XML (for other formats) serialization.
    // [COUPLING] Hard dependency on cereal library - must be ported for non-C++ targets.
    // undo / redo stack recovery
    template<class Archive> void save(Archive& ar) const
    {
        ar(size_in_mm, per_glyph, align.first, align.second);
        cereal::save(ar, char_gap);
        cereal::save(ar, line_gap);
        cereal::save(ar, boldness);
        cereal::save(ar, skew);
        cereal::save(ar, collection_number);
    }
    template<class Archive> void load(Archive& ar)
    {
        ar(size_in_mm, per_glyph, align.first, align.second);
        cereal::load(ar, char_gap);
        cereal::load(ar, line_gap);
        cereal::load(ar, boldness);
        cereal::load(ar, skew);
        cereal::load(ar, collection_number);
    }
};

/// <summary>
/// Style of embossed text
/// (Path + Type) must define how to open font for using on different OS
/// NOTE: OnEdit fix serializations: EmbossStylesSerializable, TextConfigurationSerialization
/// </summary>
// [INTENT] Encapsulates all font styling information for embossed 3D text.
// [MEMORY] String members use std::string - SSO may apply for short paths/names.
// [COUPLING] Type enum defines platform-specific font descriptor handling.
struct EmbossStyle
{
    // Human readable name of style it is shown in GUI
    std::string name;

    // [INTENT] Font identifier - meaning depends on 'type' field.
    // Could be: file path, wxWidgets font descriptor (platform-specific).
    std::string path;

    // Forward declaration - actual enum defined below after Type struct
    enum class Type;
    // Define what is stored in path
    Type type{Type::undefined};

    // User modification of font style
    FontProp prop;

    // [HAZARD] When name is empty, text was loaded from .3mf but may not be
    // reproducible - the font reference may point to unavailable system font.
    // [INTENT] Font source type enumeration - defines how to locate and load the font.
    // [HAZARD] wx_* variants are platform-dependent - cross-platform 3MF files using
    // these types will not work on different OS than where they were created.
    enum class Type {
        undefined = 0,

        // wx font descriptors are platform dependent
        // path is font descriptor generated by wxWidgets
        // [HAZARD] Windows-specific font descriptor format
        wx_win_font_descr, // on Windows
        // [HAZARD] Linux-specific font descriptor format
        wx_lin_font_descr, // on Linux
        // [HAZARD] macOS-specific font descriptor format
        wx_mac_font_descr, // on Max OS

        // TrueTypeFont file location on computer
        // [INTENT] Most portable option - file path to .ttf/.otf on local system.
        // [HAZARD] For privacy: only filename is stored into .3mf, not full path.
        file_path
    };

    bool operator==(const EmbossStyle& other) const
    {
        return type == other.type && prop == other.prop && name == other.name && path == other.path;
    }

    // undo / redo stack recovery
    template<class Archive> void serialize(Archive& ar) { ar(name, path, type, prop); }
};

// Emboss style name inside vector is unique
// It is not map beacuse items has own order (view inside of slect)
// It is stored into AppConfig by EmbossStylesSerializable
using EmbossStyles = std::vector<EmbossStyle>;

/// <summary>
/// Define how to create 'Text volume'
/// It is stored into .3mf by TextConfigurationSerialization
/// It is part of ModelVolume optional data
/// </summary>
// [INTENT] Top-level container for all text embossing configuration.
// [COUPLING] Embedded in ModelVolume as optional data - stored in 3MF via serialization.
// [MEMORY] Contains std::string for text content - SSO may apply for short strings.
struct TextConfiguration
{
    // Style of embossed text
    EmbossStyle style;

    // Embossed text value
    // [STATE] Mutable - text content can change without recreating entire configuration.
    std::string text = "None";

    // undo / redo stack recovery
    // [COUPLING] Cereal serialization - same dependency as FontProp.
    template<class Archive> void serialize(Archive& ar) { ar(style, text); }
};

} // namespace Slic3r

#endif // slic3r_TextConfiguration_hpp_
