#include <internal/files.h>
#include <internal/iouring.h>
#include <internal/handle.h>

#include <wolf.h>
#include <files.h>

#include <cassert>
#include <algorithm>

#include <sys/stat.h>

namespace wolf::internal {

FileSubsystem::FileSubsystem(EventLoop &loop, IOUring &ring, int thread_id):
    loop_(loop), ring_(ring), thread_id_(thread_id), files_(10), inflight_io_(10)
{
    for (int i = 0; i < files_.size(); i++) {
        free_files_.push_back(i);
    }

    for (int i = 0; i < inflight_io_.size(); i++) {
        free_inflight_io_.push_back(i);
    }
}


/**
 * @internal
 * @brief initiates a file open getting a free File struct and then pushing an openat SQE.
 */
void FileSubsystem::open(const char *path, FileOpenMode mode, FileOpenOptions options, int perms,
                              void *context, OnOpen on_open)
{
    if (free_files_.empty()) {
        int size = files_.size();
        files_.resize(size * 2);
        for (int i = (size * 2) - 1; i >= size; i--) {
            free_files_.push_back(i);
        }
    }

    int index = free_files_.back();
    free_files_.pop_back();

    uint32_t flags = static_cast<uint32_t>(mode) | static_cast<uint32_t>(options);

    files_[index].fd = -1;
    files_[index].ctx = context;
    files_[index].on_open = on_open;
    files_[index].on_read = nullptr;
    files_[index].on_write = nullptr;
    files_[index].on_close = nullptr;
    files_[index].inflight_ops = 0;
    files_[index].flags = flags;
    files_[index].ops_queue.clear();

    Handle handle = internal::make_handle(thread_id_, index, files_[index].gen);
    ring_.sq_push_openat(path, flags, perms, set_op(handle, internal::Op::FileOpen));
}


/**
 * @internal
 * @brief handles an open file CQE
 */
void FileSubsystem::handle_open(Handle handle, int result, uint32_t flags) {
    File &file = files_[get_index(handle)];
    // TODO: deal with closing file while opening
    if (file.gen != get_gen(handle)) {
        return;
    }

    if (result < 0) {
        // TODO: handle this
        assert(false);
    }

    file.fd = result;

    FileView view{clear_op(handle), loop_};
    file.on_open(view, file.ctx, FileError::Ok);
}


/**
 * @internal
 * @brief Reads from the given offset
 */
void FileSubsystem::read_from(Handle handle, uint64_t off, uint8_t *buf, uint64_t len, uint64_t token) {
    // TODO: deal with OOB index
    File &file = files_[get_index(handle)];

    // stale handle
    if (get_gen(handle) != file.gen) [[unlikely]] {
        return;
    }

    if (file.inflight_ops >= MAX_INFLIGHT_FILE_OPS) {
        // we must reject this operation
        if (file.ops_queue.full()) {
            FileView view{handle, loop_};
            file.on_read(view, file.ctx, std::span<uint8_t>{buf, len}, off, token, FileError::BufferFull);
            return;
        }

        file.ops_queue.push(File::FileOp{
            .type = FileIOType::Read,
            .off = off,
            .len = len,
            .buf = buf,
            .token = token,
            .flags = 0
        });

        return;
    }

    int inflight_index = get_free_inflight();
    InFlightFileIO &inflight = inflight_io_[inflight_index];

    inflight.file_index = get_index(handle);
    inflight.file_gen = get_gen(handle);
    inflight.type = FileIOType::Read;
    inflight.flags = 0;

    inflight.off = off;
    inflight.buf = buf;
    inflight.len = len;
    inflight.token = token;

    inflight.completed = 0;

    file.inflight_ops++;

    ring_.sq_push_pread(file.fd, off, buf, std::min((uint64_t)MAX_FILE_OP_SQE_SIZE, len), inflight.flags,
            set_op(make_handle(thread_id_, inflight_index, inflight.op_gen), Op::FileIO));
}


/**
 * @internal
 * @brief Writes to the given offset
 */
void FileSubsystem::write_to(Handle handle, uint64_t off, const uint8_t *buf, uint64_t len, uint64_t token) {
    // TODO: deal with OOB index
    File &file = files_[get_index(handle)];

    // stale handle
    if (get_gen(handle) != file.gen) [[unlikely]] {
        return;
    }

    if (file.inflight_ops >= MAX_INFLIGHT_FILE_OPS) {
        // we must reject this operation
        if (file.ops_queue.full()) {
            FileView view{handle, loop_};
            file.on_write(view, file.ctx, std::span<const uint8_t>{buf, len}, off, token, FileError::BufferFull);
            return;
        }

        file.ops_queue.push(File::FileOp{
            .type = FileIOType::Write,
            .off = off,
            .len = len,
            .buf = (uint8_t*)buf,
            .token = token,
            .flags = 0
        });

        return;
    }

    int inflight_index = get_free_inflight();
    InFlightFileIO &inflight = inflight_io_[inflight_index];

    inflight.file_index = get_index(handle);
    inflight.file_gen = get_gen(handle);
    inflight.type = FileIOType::Write;
    inflight.flags = 0;

    inflight.off = off;
    inflight.buf = (uint8_t*)buf;
    inflight.len = len;
    inflight.token = token;

    inflight.completed = 0;

    file.inflight_ops++;

    ring_.sq_push_pwrite(file.fd, off, buf, std::min((uint64_t)MAX_FILE_OP_SQE_SIZE, len), inflight.flags,
            set_op(make_handle(thread_id_, inflight_index, inflight.op_gen), Op::FileIO));
}


/**
 * @internal
 * @brief Handle file IO CQEs
 */
void FileSubsystem::handle_io(Handle handle, int result, uint32_t flags) {
    InFlightFileIO &inflight = inflight_io_[get_index(handle)];
    if (inflight.op_gen != get_gen(handle)) [[unlikely]] {
        return;
    }

    File &file = files_[inflight.file_index];
    if (file.gen != inflight.file_gen) [[unlikely]] {
        inflight.op_gen = (inflight.op_gen + 1) & GENERATION_MASK;
        free_inflight_io_.push_back(get_index(handle));
        return;
    }

    switch (inflight.type) {
    case FileIOType::Read:
        handle_read_from(inflight, get_index(handle), file, result, flags);
        break;
    case FileIOType::Write:
        handle_write_to(inflight, get_index(handle), file, result, flags);
        break;
    }
}

void FileSubsystem::set_onread(Handle handle, OnRead on_read) {
    File &file = files_[get_index(handle)];
    if (file.gen != get_gen(handle)) [[unlikely]] {
        return;
    }

    file.on_read = on_read;
}

void FileSubsystem::set_onwrite(Handle handle, OnWrite on_write) {
    File &file = files_[get_index(handle)];
    if (file.gen != get_gen(handle)) [[unlikely]] {
        return;
    }

    file.on_write = on_write;
}


/**
 * @internal
 * @brief Handles inflight read CQEs
 */
void FileSubsystem::handle_read_from(InFlightFileIO &inflight, int inflight_index, File &file, int result, uint32_t flags) {
    if (result < 0) [[unlikely]] {
        // TODO: set real error
        FileView view{make_handle(thread_id_, inflight.file_index, file.gen), loop_};
        file.on_read(view, file.ctx, std::span{inflight.buf, inflight.completed}, inflight.off, inflight.token, FileError::Unknown);

        file.inflight_ops--;

        // free the slot
        inflight.op_gen = (inflight.op_gen + 1) & GENERATION_MASK;
        free_inflight_io_.push_back(inflight_index);

        maybe_issue_from_queue(file, inflight.file_index);
        return;
    }

    uint32_t requested = std::min((uint64_t)MAX_FILE_OP_SQE_SIZE, inflight.len - inflight.completed);
    inflight.completed += result;
    if (result < requested || inflight.completed == inflight.len) {
        FileView view{make_handle(thread_id_, inflight.file_index, file.gen), loop_};
        file.on_read(view, file.ctx, std::span{inflight.buf, inflight.completed}, inflight.off, inflight.token, FileError::Ok);

        file.inflight_ops--;

        // free the slot
        inflight.op_gen = (inflight.op_gen + 1) & GENERATION_MASK;
        free_inflight_io_.push_back(inflight_index);

        maybe_issue_from_queue(file, inflight.file_index);
        return;
    }

    uint64_t off = inflight.off + inflight.completed;
    uint8_t *buf = inflight.buf + inflight.completed;
    uint64_t len = std::min((uint64_t)MAX_FILE_OP_SQE_SIZE, inflight.len - inflight.completed);
    ring_.sq_push_pread(file.fd, off, buf, len, inflight.flags,
        set_op(make_handle(thread_id_, inflight_index, inflight.op_gen), Op::FileIO));
}

void FileSubsystem::handle_write_to(InFlightFileIO &inflight, int inflight_index, File &file, int result, uint32_t flags) {
    if (result < 0) [[unlikely]] {
        // TODO: set real error
        FileView view{make_handle(thread_id_, inflight.file_index, file.gen), loop_};
        file.on_write(view, file.ctx, std::span{inflight.buf, inflight.completed}, inflight.off, inflight.token, FileError::Unknown);

        file.inflight_ops--;

        // free the slot
        inflight.op_gen = (inflight.op_gen + 1) & GENERATION_MASK;
        free_inflight_io_.push_back(inflight_index);

        maybe_issue_from_queue(file, inflight.file_index);
        return;
    }

    uint32_t requested = std::min((uint64_t)MAX_FILE_OP_SQE_SIZE, inflight.len - inflight.completed);
    inflight.completed += result;

    if (result < requested || inflight.completed == inflight.len) {
        FileView view{make_handle(thread_id_, inflight.file_index, file.gen), loop_};
        file.on_write(view, file.ctx, std::span{inflight.buf, inflight.completed}, inflight.off, inflight.token, FileError::Ok);

        file.inflight_ops--;

        // free the slot
        inflight.op_gen = (inflight.op_gen + 1) & GENERATION_MASK;
        free_inflight_io_.push_back(inflight_index);

        maybe_issue_from_queue(file, inflight.file_index);
        return;
    }

    uint64_t off = inflight.off + inflight.completed;
    uint8_t *buf = inflight.buf + inflight.completed;
    uint64_t len = std::min((uint64_t)MAX_FILE_OP_SQE_SIZE, inflight.len - inflight.completed);
    ring_.sq_push_pwrite(file.fd, off, buf, len, inflight.flags,
        set_op(make_handle(thread_id_, inflight_index, inflight.op_gen), Op::FileIO));
}

/**
 * @internal
 * @brief Retrives a free inflight struct index, allocating more if there isn't one available
 */
int FileSubsystem::get_free_inflight() {
    if (free_inflight_io_.size() == 0) [[unlikely]] {
        size_t size = inflight_io_.size(); 
        inflight_io_.resize(size * 2);

        for (int i = (size * 2) - 1; i >= size; i--) {
            free_inflight_io_.push_back(i);
        }
    }
    
    int index = free_inflight_io_.back();
    free_inflight_io_.pop_back();
    return index;
}


/**
 * @internal
 * @brief Checks if more ops can be submitted from the file's IO queue and submits them if so
 */
void FileSubsystem::maybe_issue_from_queue(File &file, int file_index) {
    if (file.ops_queue.empty()) return;

    while (!file.ops_queue.empty() && file.inflight_ops < MAX_INFLIGHT_FILE_OPS) {
        File::FileOp &op = file.ops_queue.peek();

        int inflight_index = get_free_inflight();
        InFlightFileIO &inflight = inflight_io_[inflight_index];

        inflight.file_index = file_index;
        inflight.file_gen = file.gen;
        inflight.type = op.type;
        inflight.flags = op.flags;

        inflight.off = op.off;
        inflight.buf = op.buf;
        inflight.len = op.len;
        inflight.token = op.token;

        inflight.completed = 0;

        file.inflight_ops++;

        switch (op.type) {
        case FileIOType::Read:
            ring_.sq_push_pread(file.fd, op.off, op.buf, std::min((uint64_t)MAX_FILE_OP_SQE_SIZE, op.len), op.flags,
                set_op(make_handle(thread_id_, inflight_index, inflight.op_gen), Op::FileIO));
            break;
        case FileIOType::Write:
            ring_.sq_push_pwrite(file.fd, op.off, op.buf, std::min((uint64_t)MAX_FILE_OP_SQE_SIZE, op.len), op.flags,
                set_op(make_handle(thread_id_, inflight_index, inflight.op_gen), Op::FileIO));
            break;
        }

        file.ops_queue.pop();
    }
}


} // namespace wolf::internal

namespace wolf {

void FileView::read_from(size_t off, std::span<uint8_t> buf, uint64_t token) {
    loop_.file_read_from(handle_, off, buf.data(), buf.size(), token);
}

void FileView::write_to(size_t off, std::span<const uint8_t> buf, uint64_t token) {
    loop_.file_write_to(handle_, off, buf.data(), buf.size(), token);
}

void FileView::set_onread(OnRead on_read) {
    loop_.file_set_onread(handle_, on_read);
}

void FileView::set_onwrite(OnWrite on_write) {
    loop_.file_set_onwrite(handle_, on_write);
}

} // namespace wolf

