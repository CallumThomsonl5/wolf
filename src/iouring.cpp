#include "internal/iouring.h"

#include <fcntl.h>
#include <linux/io_uring.h>
#include <linux/time_types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <memory>

template <typename T>
static inline T load_acquire(const T *ptr) {
    return std::atomic_load_explicit(reinterpret_cast<const std::atomic<T> *>(ptr),
                                     std::memory_order::acquire);
}

template <typename T>
static inline void store_release(T *ptr, T value) {
    std::atomic_store_explicit(reinterpret_cast<std::atomic<T> *>(ptr), value,
                               std::memory_order::release);
}

namespace wolf::internal {

/**
 * @brief Attempts to setup iouring.
 *
 * @throws std::runtime_error if there is an error setting up the ring.
 */
IOUring::IOUring(uint32_t entries) {
    struct io_uring_params params{};
    params.sq_entries = 0;
    params.cq_entries = 0;
    params.flags = IORING_SETUP_COOP_TASKRUN;

    int fd = syscall(SYS_io_uring_setup, entries, &params);
    if (fd < 0) {
        throw std::runtime_error("io_uring_setup: failed");
    }
    fd_.fd = fd;

    if ((params.features & IORING_FEAT_NODROP) == 0) {
        throw std::runtime_error("io_uring_setup: no drop not supported");
    }

    sq_size_ = params.sq_entries;
    cq_size_ = params.cq_entries;

    size_t sq_mmap_size = params.sq_off.array + sq_size_ * sizeof(uint32_t);
    void *sq_ptr = mmap(0, sq_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
                        IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) {
        throw std::runtime_error("mmap: failed to mmap sqring");
    }
    sq_ptr_ = std::unique_ptr<void, MmapDeleter>(sq_ptr, MmapDeleter(sq_mmap_size));
    sq_head_ = (uint32_t *)((uint8_t *)sq_ptr + params.sq_off.head);
    sq_tail_ = (uint32_t *)((uint8_t *)sq_ptr + params.sq_off.tail);
    sq_mask_ = *(uint32_t *)((uint8_t *)sq_ptr + params.sq_off.ring_mask);
    sq_array_ = (uint32_t *)((uint8_t *)sq_ptr + params.sq_off.array);

    size_t sq_sqes_mmap_size = sq_size_ * sizeof(struct io_uring_sqe);
    void *sq_sqes = (io_uring_sqe *)mmap(0, sq_sqes_mmap_size, PROT_READ | PROT_WRITE,
                                         MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    if (sq_sqes == MAP_FAILED) {
        throw std::runtime_error("mmap: failed to mmap sqentries");
    }
    sq_sqes_ = std::unique_ptr<io_uring_sqe[], MmapDeleter>((io_uring_sqe *)sq_sqes,
                                                            MmapDeleter(sq_sqes_mmap_size));

    size_t cq_mmap_size = params.cq_off.cqes + cq_size_ * sizeof(struct io_uring_cqe);
    void *cq_ptr = mmap(0, cq_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
                        IORING_OFF_CQ_RING);
    if (cq_ptr == MAP_FAILED) {
        throw std::runtime_error("mmap: failed to mmap cqring");
    }
    cq_ptr_ = std::unique_ptr<void, MmapDeleter>(cq_ptr, MmapDeleter(cq_mmap_size));
    cq_head_ = (uint32_t *)((uint8_t *)cq_ptr + params.cq_off.head);
    cq_tail_ = (uint32_t *)((uint8_t *)cq_ptr + params.cq_off.tail);
    cq_mask_ = *(uint32_t *)((uint8_t *)cq_ptr + params.cq_off.ring_mask);
    cq_array_ = (io_uring_cqe *)((uint8_t *)cq_ptr + params.cq_off.cqes);
}

/**
 * @brief Attempts to submit SQEs
 *
 */
int IOUring::enter() {
    // Kernel >= 5.11
    io_uring_getevents_arg arg{};
    int ret = io_uring_enter(fd_.fd, to_submit_, 0, IORING_ENTER_EXT_ARG | IORING_ENTER_GETEVENTS,
                             std::bit_cast<sigset_t *>(&arg), sizeof(arg));
    if (ret >= 0) {
        to_submit_ -= ret;
        return 0;
    } else {
        return ret;
    }
}

/**
 * @brief Attempts to submit SQEs then waits for at least one completion.
 *
 * Never times out unlike enter_timeout.
 */
int IOUring::enter_wait() {
    // Kernel >= 5.11
    io_uring_getevents_arg arg{};
    int ret = io_uring_enter(fd_.fd, to_submit_, 1, IORING_ENTER_EXT_ARG | IORING_ENTER_GETEVENTS,
                             std::bit_cast<sigset_t *>(&arg), sizeof(arg));
    if (ret >= 0) {
        to_submit_ -= ret;
        return 0;
    } else {
        return ret;
    }
}

/**
 * @brief Checks if the SQ is full.
 *
 * Must be used in the context of @ref sq_start_push().
 */
bool IOUring::sq_full() const { return sq_new_tail_ - load_acquire(sq_head_) >= sq_size_; }

/**
 * @brief Makes sure there's space for at least one submission by calling enter
 */
void IOUring::sq_ensure_space() {
    if (sq_full()) {
        sq_end_push();
        enter();
        sq_start_push();
    }
}

io_uring_sqe &IOUring::sq_slot() {
    sq_ensure_space();
    uint32_t index = sq_new_tail_++ & sq_mask_;
    sq_sqes_[index] = {};
    sq_array_[index] = index;
    to_submit_++;
    return sq_sqes_[index];
}

void IOUring::sq_push_accept(int fd, uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_ACCEPT;
    sqe.ioprio = IORING_ACCEPT_MULTISHOT;
    sqe.fd = fd;
    sqe.user_data = user_data;
}

void IOUring::sq_push_read(int fd, uint8_t *buf, uint32_t size, uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_READ;
    sqe.fd = fd;
    sqe.addr = reinterpret_cast<uint64_t>(buf);
    sqe.len = size;
    sqe.user_data = user_data;
}

void IOUring::sq_push_write(int fd, uint8_t *buf, uint32_t size,
                            uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_WRITE;
    sqe.fd = fd;
    sqe.addr = reinterpret_cast<uint64_t>(buf);
    sqe.len = size;
    sqe.user_data = user_data;
}

void IOUring::sq_push_send(int fd, uint8_t *buf, uint32_t size, uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_SEND;
    sqe.fd = fd;
    sqe.addr = reinterpret_cast<uint64_t>(buf);
    sqe.len = size;
    sqe.msg_flags = MSG_NOSIGNAL;
    sqe.user_data = user_data;
}

void IOUring::sq_push_recv(int fd, uint8_t *buf, uint32_t size, uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_RECV;
    sqe.fd = fd;
    sqe.addr = reinterpret_cast<uint64_t>(buf);
    sqe.len = size;
    sqe.user_data = user_data;
}

void IOUring::sq_push_connect(int fd, sockaddr *addr, size_t sockaddr_size,
                              uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_CONNECT;
                       sqe.fd = fd;
                       sqe.off = sockaddr_size;
                       sqe.addr = reinterpret_cast<uint64_t>(addr);
                       sqe.user_data = user_data;
}

void IOUring::sq_push_socket(int domain, int type, int protocol, uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_SOCKET;
    sqe.fd = domain;
    sqe.off = static_cast<uint64_t>(type);
    sqe.len = static_cast<uint32_t>(protocol);
    sqe.user_data = user_data;
}

void IOUring::sq_push_shutdown(int fd, int how, uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_SHUTDOWN;
    sqe.fd = fd;
    sqe.len = static_cast<uint32_t>(how);
    sqe.user_data = user_data;
}

void IOUring::sq_push_openat(const char *path, uint32_t flags, uint32_t mode,
                             uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_OPENAT;
    sqe.fd = AT_FDCWD;
    sqe.addr = reinterpret_cast<uint64_t>(path);
    sqe.len = mode;
    sqe.open_flags = flags;
    sqe.user_data = user_data;
}

void IOUring::sq_push_pread(int fd, size_t pos, uint8_t *buf, uint32_t size,
                           uint32_t flags, uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_READ;
    sqe.fd = fd;
    sqe.off = pos;
    sqe.addr = reinterpret_cast<uint64_t>(buf);
    sqe.len = size;
    sqe.rw_flags = flags;
    sqe.user_data = user_data;
}

void IOUring::sq_push_pwrite(int fd, size_t pos, const uint8_t *buf, uint32_t size,
                            uint32_t flags, uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_WRITE;
    sqe.fd = fd;
    sqe.off = pos;
    sqe.addr = reinterpret_cast<uint64_t>(buf);
    sqe.len = size;
    sqe.rw_flags = flags;
    sqe.user_data = user_data;
}

void IOUring::sq_push_close(int fd, uint64_t user_data) {
    io_uring_sqe &sqe = sq_slot();
    sqe.opcode = IORING_OP_CLOSE;
    sqe.fd = fd;
    sqe.user_data = user_data;
}

/**
 * @brief Initialises pushing SQEs to the SQ.
 *
 * Must be called prior to a series of @ref sq_push().
 */
void IOUring::sq_start_push() { sq_new_tail_ = load_acquire(sq_tail_); }

/**
 * @brief Finialises SQ pushes.
 *
 * Must be called after a series of @ref sq_push() calls.
 */
void IOUring::sq_end_push() { store_release(sq_tail_, sq_new_tail_); }

/**
 * @brief Takes a CQE off the CQ.
 *
 * Prior to a series of cq_pop() calls, @ref cq_start_pop() must be called.
 * After a series of cq_pop() calls, @ref cq_end_pop() must be called.
 *
 * @return A pointer to an io_uring_cqe, or nullptr if the CQ is empty. The
 * pointer becomes invalid after a call to @ref cq_end_pop().
 */
io_uring_cqe *IOUring::cq_pop() {
    // CQ is empty
    // Barrier to make sure that cq_save_head_ is updated prior to load
    // of tail
    if (cq_new_head_ == load_acquire(cq_tail_)) {
        return nullptr;
    }
    return &cq_array_[cq_new_head_++ & cq_mask_];
}

/**
 * @brief Initialises popping from CQ.
 *
 * @see cq_pop() for details.
 */
void IOUring::cq_start_pop() { cq_new_head_ = load_acquire(cq_head_); }

/**
 * @brief Finishes popping from CQ.
 *
 * @see cq_pop() for details.
 */
void IOUring::cq_end_pop() { store_release(cq_head_, cq_new_head_); }

} // namespace wolf::internal

