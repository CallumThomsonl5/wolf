#pragma once

#include <files.h>

#include <internal/bounded_ring_buffer.h>
#include <internal/iouring.h>

#include <vector>
#include <cstdint>

// Forward declare
namespace wolf {
class EventLoop;
}

namespace wolf::internal {

constexpr int MAX_INFLIGHT_FILE_OPS = 64;
constexpr int MAX_FILE_OP_SQE_SIZE = 262144; // 256KiB

enum class FileIOType : uint8_t {
    Read,
    Write
};

/**
 * @internal
 * @brief Internal File representation
 */
struct File {
    struct FileOp {
        FileIOType type;
        uint64_t off;
        uint64_t len;
        uint8_t *buf;
        uint64_t token;
        int flags;
    };

    int fd;
    void *ctx;
    OnOpen on_open;
    OnRead on_read;
    OnWrite on_write;
    OnFileClose on_close;
    int inflight_ops;
    int flags;
    uint32_t gen;
    BoundedRingBuffer<FileOp, MAX_INFLIGHT_FILE_OPS> ops_queue;
};


/**
 * @internal
 * @brief In flight file IO looked up by index in CQE user_data
 */
struct InFlightFileIO {
    uint32_t op_gen;
    uint32_t file_index;
    uint32_t file_gen;
    FileIOType type;
    uint16_t flags;

    // Original user supplied data
    uint64_t off;
    uint8_t *buf;
    uint64_t len;
    uint64_t token;

    // Bytes read/written so far
    uint32_t completed;
};


/**
 * @internal
 * @brief Class containing file state and logic
 */
class FileSubsystem {
public:
    FileSubsystem(EventLoop &loop, IOUring &ring, int thread_id);

    void open(const char *path, FileOpenMode mode, FileOpenOptions options, int perms,
                   void *context, OnOpen on_open);
    void handle_open(Handle handle, int result, uint32_t flags);

    void read_from(Handle handle, uint64_t off, uint8_t *buf, uint64_t len, uint64_t token);
    void write_to();
    void handle_io(Handle handle, int result, uint32_t flags);

    void set_onread(Handle handle, OnRead on_read);

private:
    void handle_read_from(InFlightFileIO &inflight, int inflight_index, File &file, int result, uint32_t flags);
    void handle_write_to(InFlightFileIO &inflight, int inflight_index, File &file, int result, uint32_t flags);

    int get_free_inflight();
    void maybe_issue_from_queue(File &file, int file_index);


    EventLoop &loop_;
    IOUring &ring_;
    int thread_id_;

    std::vector<File> files_;
    std::vector<int> free_files_;

    std::vector<InFlightFileIO> inflight_io_;
    std::vector<int> free_inflight_io_;
};

} // namespace wolf::internal
