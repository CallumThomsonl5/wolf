#pragma once

#include <types.h>

#include <fcntl.h>

#include <cstdint>
#include <span>

namespace wolf {

class EventLoop;
class FileView;
enum class FileError;

using OnOpen = void (*)(FileView file, void *context, FileError err);
using OnFileClose = void (*)();
using OnRead = void (*)(FileView file,
                        void *file_ctx,
                        std::span<uint8_t> buf,
                        uint64_t offset,
                        uint64_t token,
                        FileError err);
using OnWrite = void (*)();
using OnSize = void (*)();

enum class FileOpenMode { Read = O_RDONLY, Write = O_WRONLY, RdWr = O_RDWR };

/**
 * @brief Errors relating to files
 */
enum class FileError {
    Ok = 0,
    BufferFull,
    DoesNotExist,
    InsufficientPermissions,
    AlreadyExists,
    IsDirectory,
    NoSpace,
    Unknown
};

enum class FileOpenOptions {
    None = 0,
    Create = O_CREAT,
    Direct = O_DIRECT,
    Tmp = O_TMPFILE,
    Trunc = O_TRUNC,
    Sync = O_SYNC
};

inline constexpr FileOpenOptions operator|(FileOpenOptions a, FileOpenOptions b) {
    return static_cast<FileOpenOptions>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr void operator|=(FileOpenOptions &a, FileOpenOptions b) { a = a | b; }


/*
 * @brief Public facing file handle type
 */
class FileView {
public:
    FileView(Handle handle, EventLoop &loop) : handle_(handle), loop_(loop) {}

    void read_from(size_t off, std::span<uint8_t> buf, uint64_t token = 0);
    void write_to(size_t off, std::span<const uint8_t>, uint64_t token = 0);

    void set_onread(OnRead on_read);

    void get_size(OnSize on_size);

    void close();

    EventLoop &loop() { return loop_; }

private:
    Handle handle_;
    EventLoop &loop_;
};

} // namespace wolf

