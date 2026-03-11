#ifndef ZIPPER_HPP
#define ZIPPER_HPP

#include <cstdint>
#include <string>
#include <memory>

// [INTENT] ZIP archive creation utility - used for 3MF, SL1, and other archive formats.
// Implements the ZIP file format for packaging multiple files into a single archive.
// [COUPLING] Uses pimpl idiom (Impl*) to hide miniz implementation details.
// [MEMORY] unique_ptr manages Impl lifetime - RAII ensures archive is finalized on destruction.

namespace Slic3r {

// Class for creating zip archives.
// [INTENT] Provides simple API for creating ZIP files with compression options.
// Supports adding files, directories (trailing slash), and binary data.
class Zipper
{
public:
    // Three compression levels supported
    // [INTENT] NO_COMPRESSION = store only, FAST = deflate quick, TIGHT = deflate best ratio
    enum e_compression { NO_COMPRESSION, FAST_COMPRESSION, TIGHT_COMPRESSION };

private:
    // [MEMORY] Pimpl pattern - hides miniz library implementation details.
    class Impl;
    std::unique_ptr<Impl> m_impl;
    std::string           m_data;
    std::string           m_entry;
    e_compression         m_compression;

public:
    // Will blow up in a runtime exception if the file cannot be created.
    // [MEMORY] Opens file handle immediately - throws on failure.
    explicit Zipper(const std::string& zipfname, e_compression level = FAST_COMPRESSION);
    ~Zipper();

    // No copies allowed, this is a file resource...
    // [HAZARD] Copy constructor/assignment deleted - ensures single owner of file handle.
    Zipper(const Zipper&)            = delete;
    Zipper& operator=(const Zipper&) = delete;

    // Moving is fine.
    // [MEMORY] Move semantics enabled - transfers file ownership.
    Zipper(Zipper&& m);
    Zipper& operator=(Zipper&& m);

    /// Adding an entry means a file inside the new archive. Name param is the
    /// name of the new file. To create directories, append a forward slash.
    /// The previous entry is finished (see finish_entry)
    void add_entry(const std::string& name);

    /// Add a new binary file entry with an instantly given byte buffer.
    /// This method throws exactly like finish_entry() does.
    void add_entry(const std::string& name, const void* data, size_t bytes);

    // Writing data to the archive works like with standard streams. The target
    // within the zip file is the entry created with the add_entry method.

    // Template taking only arithmetic values, that std::to_string can handle.
    template<class T> inline typename std::enable_if<std::is_arithmetic<T>::value, Zipper&>::type operator<<(T&& val)
    {
        return this->operator<<(std::to_string(std::forward<T>(val)));
    }

    // Template applied only for types that std::string can handle for append
    // and copy. This includes c style strings...
    template<class T> inline typename std::enable_if<!std::is_arithmetic<T>::value, Zipper&>::type operator<<(T&& val)
    {
        if (m_data.empty())
            m_data = std::forward<T>(val);
        else
            m_data.append(val);
        return *this;
    }

    /// Finishing an entry means that subsequent writes will no longer be
    /// appended to the previous entry. They will be written into the internal
    /// buffer and ones an entry is added, the buffer will bind to the new entry
    /// If the buffer was written, but no entry was added, the buffer will be
    /// cleared after this call.
    ///
    /// This method will throw a runtime exception if an error occures. The
    /// entry will still be open (with the data intact) but the state of the
    /// file is up to minz after the erroneous write.
    void finish_entry();

    void finalize();

    const std::string& get_filename() const;
};

} // namespace Slic3r

#endif // ZIPPER_HPP
