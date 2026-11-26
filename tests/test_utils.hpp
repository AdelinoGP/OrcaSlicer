#ifndef SLIC3R_TEST_UTILS
#define SLIC3R_TEST_UTILS

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/Format/OBJ.hpp>

#include <boost/filesystem.hpp>

#include <clocale>
#ifdef __APPLE__
#include <xlocale.h>
#endif

#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR R"(\)"
#else
#define PATH_SEPARATOR R"(/)"
#endif

inline Slic3r::TriangleMesh load_model(const std::string &obj_filename)
{
    Slic3r::TriangleMesh mesh;
    auto fpath = TEST_DATA_DIR PATH_SEPARATOR + obj_filename;
    Slic3r::ObjInfo obj_info;
    std::string message;
    Slic3r::load_obj(fpath.c_str(), &mesh, obj_info, message);
    return mesh;
}

// RAII holder for a unique temporary file path, removed when the guard goes out
// of scope so a failing assertion never leaks it. Uses the system temp dir with
// a unique name (parallel-safe, cross-platform). The file itself is created by
// whoever writes to path()/string(); this only reserves the name and cleans up.
class ScopedTemporaryFile
{
public:
    explicit ScopedTemporaryFile(const std::string &extension = ".tmp")
        : m_path(boost::filesystem::temp_directory_path()
                 / boost::filesystem::unique_path("orca-%%%%-%%%%-%%%%" + extension))
    {}
    ~ScopedTemporaryFile() { boost::system::error_code ec; boost::filesystem::remove(m_path, ec); }
    ScopedTemporaryFile(const ScopedTemporaryFile &) = delete;
    ScopedTemporaryFile &operator=(const ScopedTemporaryFile &) = delete;

    const boost::filesystem::path &path() const { return m_path; }
    std::string string() const { return m_path.string(); }

private:
    boost::filesystem::path m_path;
};

// A temp directory created on construction and removed recursively on scope exit.
class ScopedTemporaryDir
{
public:
    explicit ScopedTemporaryDir(const std::string &prefix = "orca")
        : m_path(boost::filesystem::temp_directory_path()
                 / boost::filesystem::unique_path(prefix + "-%%%%-%%%%-%%%%"))
    { boost::filesystem::create_directories(m_path); }
    ~ScopedTemporaryDir() { boost::system::error_code ec; boost::filesystem::remove_all(m_path, ec); }
    ScopedTemporaryDir(const ScopedTemporaryDir &) = delete;
    ScopedTemporaryDir &operator=(const ScopedTemporaryDir &) = delete;

    const boost::filesystem::path &path() const { return m_path; }
    std::string string() const { return m_path.string(); }

private:
    boost::filesystem::path m_path;
};

// PNP fork (UpstreamSyncMap ticket 06): CommaNumericLocale from upstream's test_utils.hpp, needed by
// the ported test_locales_utils.cpp.
struct CommaNumericLocale
{
#ifdef _WIN32
    bool apply()
    {
        for (const char* name : { "de-DE", "German_Germany.1252" })
            if (std::setlocale(LC_NUMERIC, name) != nullptr)
                return true;
        return false;
    }
#else
    locale_t locale { (locale_t) 0 };
    bool apply()
    {
        for (const char* name : { "de_DE.UTF-8", "de_DE.utf8", "de_DE" })
            if ((locale = newlocale(LC_NUMERIC_MASK, name, (locale_t) 0)) != (locale_t) 0) {
                uselocale(locale);
                return true;
            }
        return false;
    }
    // Freed once the setters around apply() have put the thread's own locale back.
    ~CommaNumericLocale()
    {
        if (locale != (locale_t) 0)
            freelocale(locale);
    }
#endif
};

#endif // SLIC3R_TEST_UTILS
