#ifndef WOLF_IOURING_H_INCLUDED
#define WOLF_IOURING_H_INCLUDED

#include <atomic>
#include <bit>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>

#include <linux/io_uring.h>
#include <linux/time_types.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

template <typename T>
static inline T load_acquire(const T *ptr) {
    return std::atomic_load_explicit(
        reinterpret_cast<const std::atomic<T> *>(ptr),
        std::memory_order::acquire);
}

template <typename T>
static inline void store_release(T *ptr, T value) {
    std::atomic_store_explicit(reinterpret_cast<std::atomic<T> *>(ptr), value,
                               std::memory_order::release);
}

/**
 * @brief Indicates error with iouring
 */
class IOUringException : public std::exception {
public:
    IOUringException(const std::string &msg) : msg_(msg) {}
    const char *what() const noexcept override { return msg_.c_str(); }

private:
    const std::string msg_;
};

/**
 * @brief A wrapper over the io_uring interface with a simplified API and RAII.
 */
class IOUring {
public:
    IOUring(std::uint32_t entries);
    ~IOUring() = default;
    IOUring(const IOUring &) = delete;
    IOUring(IOUring &&ref);

    void enter(int timeout);

    bool sq_full() const;
    void sq_push(io_uring_sqe sqe);
    template <typename Addr, typename UserData>
        requires(sizeof(UserData) == 8 && sizeof(Addr) == 8)
    void sq_push(std::uint8_t opcode, int fd, std::uint64_t off, Addr addr,
                 std::size_t len, std::uint8_t flags, UserData user_data);
    void sq_start_push();
    void sq_end_push();

    io_uring_cqe *cq_pop();
    void cq_start_pop();
    void cq_end_pop();

private:
    class MmapDeleter {
    public:
        MmapDeleter() {}
        MmapDeleter(std::size_t size) : size_(size) {}

        void operator()(void *ptr) { munmap(ptr, size_); }

    private:
        std::size_t size_ = 0;
    };

    struct FdDeleter {
        ~FdDeleter() {
            if (fd >= 0)
                ::close(fd);
        }
        int fd = -1;
    };

    FdDeleter fd_;
    int to_submit_ = 0;

    std::uint32_t sq_size_ = 0;
    std::uint32_t cq_size_ = 0;

    std::unique_ptr<void, MmapDeleter> sq_ptr_;
    std::uint32_t *sq_head_ = nullptr;
    std::uint32_t *sq_tail_ = nullptr;
    std::uint32_t sq_new_tail_ = 0;
    std::uint32_t sq_mask_ = 0;
    std::uint32_t *sq_array_ = nullptr;
    std::unique_ptr<io_uring_sqe[], MmapDeleter> sq_sqes_;

    std::unique_ptr<void, MmapDeleter> cq_ptr_;
    std::uint32_t *cq_head_ = nullptr;
    std::uint32_t cq_new_head_ = 0;
    std::uint32_t *cq_tail_ = nullptr;
    std::uint32_t cq_mask_ = 0;
    io_uring_cqe *cq_array_ = nullptr;
};

/**
 * @brief Attempts to setup iouring.
 *
 * @throws IOUringException if there is an error setting up the ring.
 */
inline IOUring::IOUring(std::uint32_t entries) {
    struct io_uring_params params{
        .sq_entries = 0,
        .cq_entries = 0,
        .flags = IORING_SETUP_COOP_TASKRUN,
    };

    int fd = syscall(SYS_io_uring_setup, entries, &params);
    if (fd < 0) {
        throw IOUringException("io_uring_setup: failed");
    }
    fd_.fd = fd;

    sq_size_ = params.sq_entries;
    cq_size_ = params.cq_entries;

    std::size_t sq_mmap_size =
        params.sq_off.array + sq_size_ * sizeof(std::uint32_t);
    void *sq_ptr = mmap(0, sq_mmap_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) {
        throw IOUringException("mmap: failed to mmap sqring");
    }
    sq_ptr_ =
        std::unique_ptr<void, MmapDeleter>(sq_ptr, MmapDeleter(sq_mmap_size));
    sq_head_ = (std::uint32_t *)((std::uint8_t *)sq_ptr + params.sq_off.head);
    sq_tail_ = (std::uint32_t *)((std::uint8_t *)sq_ptr + params.sq_off.tail);
    sq_mask_ =
        *(std::uint32_t *)((std::uint8_t *)sq_ptr + params.sq_off.ring_mask);
    sq_array_ = (std::uint32_t *)((std::uint8_t *)sq_ptr + params.sq_off.array);

    std::size_t sq_sqes_mmap_size = sq_size_ * sizeof(struct io_uring_sqe);
    void *sq_sqes =
        (io_uring_sqe *)mmap(0, sq_sqes_mmap_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    if (sq_sqes == MAP_FAILED) {
        throw IOUringException("mmap: failed to mmap sqentries");
    }
    sq_sqes_ = std::unique_ptr<io_uring_sqe[], MmapDeleter>(
        (io_uring_sqe *)sq_sqes, MmapDeleter(sq_sqes_mmap_size));

    std::size_t cq_mmap_size =
        params.cq_off.cqes + cq_size_ * sizeof(struct io_uring_cqe);
    void *cq_ptr = mmap(0, cq_mmap_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
    if (cq_ptr == MAP_FAILED) {
        throw IOUringException("mmap: failed to mmap cqring");
    }
    cq_ptr_ =
        std::unique_ptr<void, MmapDeleter>(cq_ptr, MmapDeleter(cq_mmap_size));
    cq_head_ = (std::uint32_t *)((std::uint8_t *)cq_ptr + params.cq_off.head);
    cq_tail_ = (std::uint32_t *)((std::uint8_t *)cq_ptr + params.cq_off.tail);
    cq_mask_ =
        *(std::uint32_t *)((std::uint8_t *)cq_ptr + params.cq_off.ring_mask);
    cq_array_ = (io_uring_cqe *)((std::uint8_t *)cq_ptr + params.cq_off.cqes);
}

/**
 * @brief Attempts to submit SQs and waits for the completion of at least one
 * CQE, or timeout seconds.
 *
 * @param timeout Maximum time in seconds to block before returning.
 */
inline void IOUring::enter(int timeout) {
    // Kernel >= 5.11
    __kernel_timespec ts{.tv_sec = timeout};
    io_uring_getevents_arg arg{.ts = std::bit_cast<std::uint64_t>(&ts)};
    syscall(SYS_io_uring_enter, fd_.fd, to_submit_, 1,
            IORING_ENTER_EXT_ARG | IORING_ENTER_GETEVENTS,
            std::bit_cast<std::uint64_t>(&arg), sizeof(arg));

    to_submit_ = 0;
}

/**
 * @brief Checks if the SQ is full.
 *
 * Must be used in the context of @ref sq_start_push().
 */
inline bool IOUring::sq_full() const {
    return sq_new_tail_ - load_acquire(sq_head_) >= sq_size_;
}

/**
 * @brief Pushes SQE onto the SQ.
 *
 * Prior to a series of sq_push(), @ref sq_start_push() must be called. After a
 * series of sq_push() calls, @ref sq_end_push() must be called.
 */
inline void IOUring::sq_push(io_uring_sqe sqe) {
    std::uint32_t index = sq_new_tail_++ & sq_mask_;
    sq_sqes_[index] = sqe;
    sq_array_[index] = index;
    to_submit_++;
}

/**
 * @brief Similar to @ref sq_push() but initialises the entry in place.
 *
 * @see sq_push() for more details.
 */
template <typename Addr, typename UserData>
    requires(sizeof(UserData) == 8 && sizeof(Addr) == 8)
inline void IOUring::sq_push(std::uint8_t opcode, int fd, std::uint64_t off,
                             Addr addr, std::size_t len, std::uint8_t flags,
                             UserData user_data) {
    std::uint32_t index = sq_new_tail_++ & sq_mask_;

    sq_sqes_[index].opcode = opcode;
    sq_sqes_[index].fd = fd;
    sq_sqes_[index].off = off;
    sq_sqes_[index].addr = std::bit_cast<std::uint64_t>(addr);
    sq_sqes_[index].len = len;
    sq_sqes_[index].flags = flags;
    sq_sqes_[index].user_data = std::bit_cast<std::uint64_t>(user_data);

    sq_array_[index] = index;
    to_submit_++;
}

/**
 * @brief Initialises pushing SQEs to the SQ.
 *
 * Must be called prior to a series of @ref sq_push().
 */
inline void IOUring::sq_start_push() { sq_new_tail_ = load_acquire(sq_tail_); }

/**
 * @brief Finialises SQ pushes.
 *
 * Must be called after a series of @ref sq_push() calls.
 */
inline void IOUring::sq_end_push() { store_release(sq_tail_, sq_new_tail_); }

/**
 * @brief Takes a CQE off the CQ.
 *
 * Prior to a series of cq_pop() calls, @ref cq_start_pop() must be called.
 * After a series of cq_pop() calls, @ref cq_end_pop() must be called.
 *
 * @return A pointer to an io_uring_cqe, or nullptr if the CQ is empty. The
 * pointer becomes invalid after a call to @ref cq_end_pop().
 */
inline io_uring_cqe *IOUring::cq_pop() {
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
inline void IOUring::cq_start_pop() { cq_new_head_ = load_acquire(cq_head_); }

/**
 * @brief Finishes popping from CQ.
 *
 * @see cq_pop() for details.
 */
inline void IOUring::cq_end_pop() { store_release(cq_head_, cq_new_head_); }

#endif // WOLF_IOURING_H_INCLUDED
