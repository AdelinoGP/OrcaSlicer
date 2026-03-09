// [INTENT] GCodeReader.cpp implements the streaming G-code parser for OrcaSlicer.
// It is a performance-critical path used both for in-flight G-code post-processing
// (SpiralVase, CoolingBuffer) and for file-level analysis (GCodeProcessor).
// All parsing is done character-by-character over raw byte pointers to minimise
// allocation overhead; fast_float is used for the common numeric parsing case.
//
// [MEMORY] No dynamic allocation per line in the hot path. GCodeLine is reused via
// reset(). The file-streaming path uses a fixed 640kB buffer (65536*10 bytes).
//
// [CONCURRENCY] GCodeReader is NOT thread-safe. Each thread that needs to parse
// G-code must own a separate instance.

#include "GCodeReader.hpp"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
#include "Utils.hpp"

#include "LocalesUtils.hpp"

#include <Shiny/Shiny.h>
#include <fast_float/fast_float.h>

namespace Slic3r {

// [INTENT] Apply a GCodeConfig (relative E distances flag, etc.) to the reader
// so that coordinate tracking is consistent with the generator's settings.
// Must be called before the first parse if relative E is in use.
void GCodeReader::apply_config(const GCodeConfig& config) { m_config = config; }

void GCodeReader::apply_config(const DynamicPrintConfig& config) { m_config.apply(config, true); }

// [INTENT] Core line parser. Accepts a [ptr, end) byte range representing a single
// G-code line (no trailing newline). Extracts:
//   - command span: command.first..command.second (e.g. "G1")
//   - axis parameters: fills gline.m_axis[] and sets bits in gline.m_mask
//   - raw text copy: stored in gline.m_raw for pass-through or post-processing
// Returns a pointer to the first byte past the parsed line (start of next line).
//
// [HAZARD] fast_float::from_chars is used for numeric parsing. It is locale-independent
// and expects a decimal point (not comma). The assert(is_decimal_separator_point()) guard
// enforces this at runtime in debug builds only. On locales that use comma as decimal
// separator without this precondition check, parsing would silently produce wrong values.
//
// [HAZARD] The axis switch maps 'X','Y','Z','F','I','J','E','P' to enum Axis values.
// BBS added I and J axes for G2/G3 arc support. If the Axis enum in libslic3r.h is ever
// reordered, all array accesses via int(axis) silently corrupt the position array.
const char* GCodeReader::parse_line_internal(const char*                          ptr,
                                             const char*                          end,
                                             GCodeLine&                           gline,
                                             std::pair<const char*, const char*>& command)
{
    PROFILE_FUNC();

    assert(is_decimal_separator_point());

    // command and args
    const char* c = ptr;
    {
        PROFILE_BLOCK(command_and_args);
        // Skip the whitespaces.
        command.first = skip_whitespaces(c);
        // Skip the command.
        c = command.second = skip_word(command.first);
        // Up to the end of line or comment.
        while (!is_end_of_gcode_line(*c)) {
            // Skip whitespaces.
            c = skip_whitespaces(c);
            if (is_end_of_gcode_line(*c))
                break;
            // Check the name of the axis.
            Axis axis = NUM_AXES_WITH_UNKNOWN;
            switch (*c) {
            case 'X': axis = X; break;
            case 'Y': axis = Y; break;
            case 'Z': axis = Z; break;
            case 'F': axis = F; break;
            // BBS: add I and J axis
            case 'I': axis = I; break;
            case 'J': axis = J; break;
            case 'E': axis = E; break;
            case 'P': axis = P; break;
            default:
                if (*c >= 'A' && *c <= 'Z')
                    // Unknown axis, but we still want to remember that such a axis was seen.
                    axis = UNKNOWN_AXIS;
                break;
            }
            if (axis != NUM_AXES_WITH_UNKNOWN) {
                // Try to parse the numeric value.
                double v;
                // [INTENT] fast_float::from_chars parses floating-point values without
                // locale sensitivity and without heap allocation. C++17 structured bindings
                // capture [pointer past last consumed char, error code]. If the parse ends
                // at a word boundary (space, semicolon, NUL), the value is accepted.
                auto [pend, ec] = fast_float::from_chars(++c, end, v);
                if (pend != c && is_end_of_word(*pend)) {
                    // The axis value has been parsed correctly.
                    if (axis != UNKNOWN_AXIS)
                        gline.m_axis[int(axis)] = float(v);
                    gline.m_mask |= 1 << int(axis);
                    c = pend;
                } else
                    // Skip the rest of the word.
                    c = skip_word(c);
            } else
                // Skip the rest of the word.
                c = skip_word(c);
        }
    }

    // [INTENT] When relative E distances are in use, reset the tracked E position to 0
    // after every line that moves E. This ensures that dist_E() / new_E() computations
    // return the per-move delta, not the accumulated absolute position. This matches the
    // G-code generator's use of M83 (relative extrusion mode).
    if (gline.has(E) && m_config.use_relative_e_distances)
        m_position[E] = 0;

    // Skip the rest of the line.
    for (; !is_end_of_line(*c); ++c)
        ;

    // Copy the raw string including the comment, without the trailing newlines.
    if (c > ptr) {
        PROFILE_BLOCK(copy_raw_string);
        gline.m_raw.assign(ptr, c);
    }

    // Skip the trailing newlines.
    if (*c == '\r')
        ++c;
    if (*c == '\n')
        ++c;

    if (m_verbose)
        std::cout << gline.m_raw << std::endl;

    return c;
}

// [INTENT] After a successful parse, update the reader's tracked axis positions
// (m_position[]) based on the parsed command. Only motion commands that actually
// update machine coordinates are handled:
//   G0/G1  — linear move; updates all present axes
//   G2/G3  — arc move (BBS addition); updates all present axes (endpoint only)
//   G92    — set position (coordinate reset); updates all present axes
// Non-motion G commands (G28, G29, etc.) are intentionally ignored so m_position
// remains a best-effort reflection of the printer head's actual location.
//
// [HAZARD H834] Arc moves (G2/G3) update m_position with the arc endpoint but do NOT
// account for I/J offset parameters. Callers that compute travel distances from
// m_position changes will undercount arc path length. This matters for pressure
// advance and cooling calculations that rely on GCodeReader-tracked positions.
//
// [COUPLING] m_position[] is consumed by GCodeProcessor, CoolingBuffer, SpiralVase,
// and any external callback that calls gline.dist_*(m_reader.m_position). Must stay
// in sync with the actual coordinate system the firmware is tracking.
void GCodeReader::update_coordinates(GCodeLine& gline, std::pair<const char*, const char*>& command)
{
    PROFILE_FUNC();
    if (*command.first == 'G') {
        int cmd_len = int(command.second - command.first);
        // BBS: add support of G2 and G3
        if ((cmd_len == 2 && (command.first[1] == '0' || command.first[1] == '1' || command.first[1] == '2' || command.first[1] == '3')) ||
            (cmd_len == 3 && command.first[1] == '9' && command.first[2] == '2')) {
            for (size_t i = 0; i < NUM_AXES; ++i)
                if (gline.has(Axis(i)))
                    m_position[i] = gline.value(Axis(i));
        }
    }
}

// [INTENT] Low-level streaming file parser. Reads the file in 640 kB chunks
// (65536*10 bytes) and splits on CR/LF line boundaries. Lines that span buffer
// boundaries are accumulated in `gcode_line` (std::string) and flushed once
// complete. For each line, parse_line_callback(begin, end) is invoked with raw
// byte pointers. line_end_callback(file_pos) is called with the byte offset of
// each newline — used to build a line-end index by GCodeProcessor for seekable
// random access into large G-code files.
//
// [HAZARD H835] The line-end callback fires for '\n' only; bare '\r'-terminated
// lines (old Mac line endings) are not counted. If a file uses CR-only endings,
// the lines_ends index built by parse_file(..., lines_ends) will be wrong, causing
// GCodeProcessor's seek-by-line logic to land at incorrect byte offsets.
//
// [MEMORY] Fixed 640 kB heap allocation for the read buffer. Lines longer than
// the buffer size would never trigger eol inside the inner loop, but would be
// accumulated indefinitely in gcode_line. In practice G-code lines are < 200 bytes
// so this is not a real risk.
//
// [CONCURRENCY] m_parsing is a plain bool; setting it to false from within a
// callback (to request early exit) is the only inter-call coordination mechanism.
// Not safe to modify from a different thread.
template<typename ParseLineCallback, typename LineEndCallback>
bool GCodeReader::parse_file_raw_internal(const std::string& filename,
                                          ParseLineCallback  parse_line_callback,
                                          LineEndCallback    line_end_callback)
{
    FilePtr in{boost::nowide::fopen(filename.c_str(), "rb")};

    // Read the input stream 64kB at a time, extract lines and process them.
    std::vector<char> buffer(65536 * 10, 0);
    // Line buffer.
    std::string gcode_line;
    size_t      file_pos = 0;
    m_parsing            = true;
    for (;;) {
        size_t cnt_read = ::fread(buffer.data(), 1, buffer.size(), in.f);
        if (::ferror(in.f))
            return false;
        bool eof       = cnt_read == 0;
        auto it        = buffer.begin();
        auto it_bufend = buffer.begin() + cnt_read;
        while (it != it_bufend || (eof && !gcode_line.empty())) {
            // Find end of line.
            bool eol    = false;
            auto it_end = it;
            for (; it_end != it_bufend && !(eol = *it_end == '\r' || *it_end == '\n'); ++it_end)
                if (*it_end == '\n')
                    line_end_callback(file_pos + (it_end - buffer.begin()) + 1);
            // End of line is indicated also if end of file was reached.
            eol |= eof && it_end == it_bufend;
            if (eol) {
                if (gcode_line.empty())
                    parse_line_callback(&(*it), &(*it_end));
                else {
                    gcode_line.insert(gcode_line.end(), it, it_end);
                    parse_line_callback(gcode_line.c_str(), gcode_line.c_str() + gcode_line.size());
                    gcode_line.clear();
                }
                if (!m_parsing)
                    // The callback wishes to exit.
                    return true;
            } else
                gcode_line.insert(gcode_line.end(), it, it_end);
            // Skip EOL.
            it = it_end;
            if (it != it_bufend && *it == '\r')
                ++it;
            if (it != it_bufend && *it == '\n') {
                line_end_callback(file_pos + (it - buffer.begin()) + 1);
                ++it;
            }
        }
        if (eof)
            break;
        file_pos += cnt_read;
    }
    return true;
}

// [INTENT] Structured wrapper over parse_file_raw_internal. Reuses a single
// GCodeLine instance (reset on each line) to avoid per-line heap allocation.
// Strips optional BBS/Marlin line-number prefix ('N' word) before dispatching
// to parse_line so that command detection is not confused by the N-word.
//
// [COUPLING] parse_line_callback receives a fully-populated GCodeLine with
// m_axis[] and m_mask set, ready for callers such as GCodeProcessor to inspect
// axis values without re-parsing the raw string.
template<typename ParseLineCallback, typename LineEndCallback>
bool GCodeReader::parse_file_internal(const std::string& filename, ParseLineCallback parse_line_callback, LineEndCallback line_end_callback)
{
    GCodeLine gline;
    return this->parse_file_raw_internal(
        filename,
        [this, &gline, parse_line_callback](const char* begin, const char* end) {
            gline.reset();

            const char* begin_new = begin;
            begin_new             = skip_whitespaces(begin_new);
            if (std::toupper(*begin_new) == 'N')
                begin_new = skip_word(begin_new);
            begin_new = skip_whitespaces(begin_new);
            this->parse_line(begin_new, end, gline, parse_line_callback);
        },
        line_end_callback);
}

// [INTENT] Public entry point for full G-code file parsing with structured line
// callbacks. Delegates through parse_file_internal → parse_file_raw_internal.
// The overload accepting lines_ends builds a byte-offset index of every '\n'
// in the file; GCodeProcessor uses this to implement seekable random access
// during visualization / thumbnail extraction.
bool GCodeReader::parse_file(const std::string& file, callback_t callback)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":  before parse_file %1%") % file.c_str();
    auto ret = this->parse_file_internal(file, callback, [](size_t) {});
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":  finished parse_file %1%") % file.c_str();

    return ret;
}

bool GCodeReader::parse_file(const std::string& file, callback_t callback, std::vector<size_t>& lines_ends)
{
    lines_ends.clear();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":  before parse_file %1%") % file.c_str();
    auto ret = this->parse_file_internal(file, callback, [&lines_ends](size_t file_pos) { lines_ends.emplace_back(file_pos); });
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":  finished parse_file %1%") % file.c_str();

    return ret;
}

bool GCodeReader::parse_file_raw(const std::string& filename, raw_line_callback_t line_callback)
{
    return this->parse_file_raw_internal(
        filename, [this, line_callback](const char* begin, const char* end) { line_callback(*this, begin, end); }, [](size_t) {});
}

// [INTENT] Static helper: scans a raw G-code line string for the position of a
// named axis character (e.g. 'X', 'E'). Returns a pointer to the axis letter
// in the raw string so the caller can either check existence or extract the
// following numeric substring. Returns nullptr if the axis is not present.
//
// [COUPLING] Used by GCodeLine::axis_pos(char) and by external callers that want
// to splice/replace a specific axis value in the raw string.
const char* GCodeReader::axis_pos(const char* raw_str, char axis)
{
    const char* c = raw_str;
    // Skip the whitespaces.
    c = skip_whitespaces(c);
    // Skip the command.
    c = skip_word(c);
    // Up to the end of line or comment.
    while (!is_end_of_gcode_line(*c)) {
        // Skip whitespaces.
        c = skip_whitespaces(c);
        if (is_end_of_gcode_line(*c))
            break;
        // Check the name of the axis.
        if (*c == axis)
            return c;
        // Skip the rest of the word.
        c = skip_word(c);
    }
    return nullptr;
}

// [INTENT] GCodeLine::has(char) — char-based existence check that re-scans the
// raw string. Used when the axis letter is not in the Axis enum (e.g. 'S', 'R').
// Slower than the bitmask-based has(Axis) but correct for non-enumerated axes.
bool GCodeReader::GCodeLine::has(char axis) const
{
    const char* c = m_raw.c_str();
    // Skip the whitespaces.
    c = skip_whitespaces(c);
    // Skip the command.
    c = skip_word(c);
    // Up to the end of line or comment.
    while (!is_end_of_gcode_line(*c)) {
        // Skip whitespaces.
        c = skip_whitespaces(c);
        if (is_end_of_gcode_line(*c))
            break;
        // Check the name of the axis.
        if (*c == axis)
            return true;
        // Skip the rest of the word.
        c = skip_word(c);
    }
    return false;
}

std::string_view GCodeReader::GCodeLine::axis_pos(char axis) const
{
    const std::string& s = this->raw();
    const char*        c = GCodeReader::axis_pos(this->raw().c_str(), axis);
    return c ? std::string_view{c, s.size() - (c - s.data())} : std::string_view();
}

// [INTENT] GCodeLine::has_value(string_view, float&) — fast path value extractor
// operating on a pre-located axis_pos string_view. The ++c advances past the axis
// letter to the numeric part before calling fast_float::from_chars.
bool GCodeReader::GCodeLine::has_value(std::string_view axis_pos, float& value)
{
    if (const char* c = axis_pos.data(); c) {
        // Try to parse the numeric value.
        double      v   = 0.;
        const char* end = axis_pos.data() + axis_pos.size();
        auto [pend, ec] = fast_float::from_chars(++c, end, v);
        if (pend != c && is_end_of_word(*pend)) {
            // The axis value has been parsed correctly.
            value = float(v);
            return true;
        }
    }
    return false;
}

// [INTENT] GCodeLine::has_value(char, float&) — slow path value extractor that
// re-scans m_raw for the given axis letter then calls strtod. Used by callers
// with char-based axis letters not in the Axis enum (e.g. 'S' for fan speed).
//
// [HAZARD H836] Uses strtod (locale-dependent) rather than fast_float::from_chars.
// If the process locale uses comma as decimal separator, strtod will parse "E1.5"
// as 1.0 (stopping at the period). The assert(is_decimal_separator_point()) guard
// catches this in debug builds but not release. The fast-path has_value(string_view)
// overload above correctly uses fast_float; this overload is inconsistent.
bool GCodeReader::GCodeLine::has_value(char axis, float& value) const
{
    assert(is_decimal_separator_point());
    const char* c = m_raw.c_str();
    // Skip the whitespaces.
    c = skip_whitespaces(c);
    // Skip the command.
    c = skip_word(c);
    // Up to the end of line or comment.
    while (!is_end_of_gcode_line(*c)) {
        // Skip whitespaces.
        c = skip_whitespaces(c);
        if (is_end_of_gcode_line(*c))
            break;
        // Check the name of the axis.
        if (*c == axis) {
            // Try to parse the numeric value.
            char*  pend = nullptr;
            double v    = strtod(++c, &pend);
            if (pend != nullptr && is_end_of_word(*pend)) {
                // The axis value has been parsed correctly.
                value = float(v);
                return true;
            }
        }
        // Skip the rest of the word.
        c = skip_word(c);
    }
    return false;
}

// [INTENT] GCodeLine::set — in-place mutation of an axis value inside the raw
// G-code string. Used by SpiralVase and CoolingBuffer to rewrite Z or E values
// without re-generating the entire line. The raw string is modified with
// std::string::replace; the in-memory m_axis[] cache is also updated.
//
// [HAZARD H837] The axis-letter search uses m_raw.find(match) where match is
// " X", " Y", etc. (space + letter). If the G-code line has no leading space
// before the axis letter (e.g. "G1X10Y20"), find() will return npos (or find a
// false match later in a comment), and the +2 offset will corrupt m_raw.
// In practice OrcaSlicer always emits a space before axis letters, but firmware
// G-code or third-party slicers may not.
//
// [HAZARD H838] The 'end' search for the existing value uses m_raw.find(' ', pos+1)
// which stops at the next space. If the axis value is the last token on the line
// (no trailing space), find returns npos and the replace(pos, npos-pos, ...) will
// silently truncate the string to just the replaced value plus any comment.
void GCodeReader::GCodeLine::set(const Axis axis, const float new_value, const int decimal_digits)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimal_digits) << new_value;

    char match[3] = " X";
    if (int(axis) < 3)
        match[1] += int(axis);
    else if (axis == F)
        match[1] = 'F';
    // BBS： handle I and J axis
    else if (axis == I)
        match[1] = 'I';
    else if (axis == J)
        match[1] = 'J';
    else {
        assert(axis == E);
        match[1] = 'E';
    }

    if (this->has(axis)) {
        size_t pos = m_raw.find(match) + 2;
        size_t end = m_raw.find(' ', pos + 1);
        m_raw      = m_raw.replace(pos, end - pos, ss.str());
    } else {
        size_t pos = m_raw.find(' ');
        if (pos == std::string::npos)
            m_raw += std::string(match) + ss.str();
        else
            m_raw = m_raw.replace(pos, 0, std::string(match) + ss.str());
    }
    m_axis[axis] = new_value;
    m_mask |= 1 << int(axis);
}

} // namespace Slic3r
