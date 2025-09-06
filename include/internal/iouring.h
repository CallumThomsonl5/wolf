#pragma once

#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>

namespace wolf::internal {

inline int io_uring_enter(std::uint32_t fd, std::uint32_t to_submit, std::uint32_t min_complete,
                          std::uint32_t flags, sigset_t *sig, std::size_t sz) {

    int ret = -1;
#if defined(__x86_64__)
    register std::uint32_t r10 asm("r10") = flags;
    register sigset_t *r8 asm("r8") = sig;
    register std::size_t r9 asm("r9") = sz;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(SYS_io_uring_enter), "D"(fd), "S"(to_submit), "d"(min_complete), "r"(r10),
                   "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
#else
    ret = syscall(SYS_io_uring_enter, fd, to_submit, min_complete, flags, sig, sz);
#endif

    return ret;
}

/**
 * @brief A wrapper over the io_uring interface with a simplified API and RAII.
 */
class IOUring {
public:
    IOUring(std::uint32_t entries);
    ~IOUring() = default;
    IOUring(const IOUring &) = delete;
    IOUring(IOUring &&ref);

    template <typename Rep, typename Period>
    int enter_timeout(std::chrono::duration<Rep, Period> timeout);
    int enter();
    int enter_wait();

    bool sq_full() const;
    void sq_ensure_space();
    void sq_push(io_uring_sqe &sqe);
    void sq_push_accept(int fd, std::uint64_t user_data);
    void sq_push_read(int fd, std::uint8_t *buf, std::uint32_t size, std::uint64_t user_data);
    void sq_push_write(int fd, std::uint8_t *buf, std::uint32_t size, std::uint64_t user_data);
    void sq_push_send(int fd, std::uint8_t *buf, std::uint32_t size, std::uint64_t user_data);
    void sq_push_recv(int fd, std::uint8_t *buf, std::uint32_t size, std::uint64_t user_data);
    void sq_push_connect(int fd, sockaddr *addr, std::size_t sockaddr_size,
                         std::uint64_t user_data);
    void sq_push_socket(int domain, int type, int protocol, std::uint64_t user_data);
    void sq_push_shutdown(int fd, int how, std::uint64_t user_data);
    void sq_push_close(int fd, std::uint64_t user_data);

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

    // Memory order is intentional to prevent unnecessary padding

    FdDeleter fd_;
    int to_submit_ = 0;

    // SQ ints
    std::uint32_t sq_size_ = 0;
    std::uint32_t sq_mask_ = 0;
    std::uint32_t sq_new_tail_ = 0;

    // CQ ints
    std::uint32_t cq_size_ = 0;
    std::uint32_t cq_mask_ = 0;
    std::uint32_t cq_new_head_ = 0;

    // SQ ptrs
    std::unique_ptr<void, MmapDeleter> sq_ptr_;
    std::unique_ptr<io_uring_sqe[], MmapDeleter> sq_sqes_;
    std::uint32_t *sq_array_ = nullptr;
    std::uint32_t *sq_head_ = nullptr;
    std::uint32_t *sq_tail_ = nullptr;

    // CQ ptrs
    std::unique_ptr<void, MmapDeleter> cq_ptr_;
    io_uring_cqe *cq_array_ = nullptr;
    std::uint32_t *cq_head_ = nullptr;
    std::uint32_t *cq_tail_ = nullptr;
};

/**
 * @brief Attempts to submit SQs then waits for at least one completion, or
 * times out.
 *
 * @param timeout Maximum time to block before timing out.
 */
template <typename Rep, typename Period>
inline int IOUring::enter_timeout(std::chrono::duration<Rep, Period> timeout) {
    // Kernel >= 5.11
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - secs);
    __kernel_timespec ts{.tv_sec = secs.count(), .tv_nsec = ns.count()};
    io_uring_getevents_arg arg{.ts = std::bit_cast<std::uint64_t>(&ts)};
    int ret = io_uring_enter(fd_.fd, to_submit_, 1, IORING_ENTER_EXT_ARG | IORING_ENTER_GETEVENTS,
                             std::bit_cast<sigset_t *>(&arg), sizeof(arg));
    if (ret >= 0) {
        to_submit_ -= ret;
        return 0;
    } else {
        return ret;
    }
}

} // namespace wolf::internal