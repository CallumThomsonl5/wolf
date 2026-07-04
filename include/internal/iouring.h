#pragma once

#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>

namespace wolf::internal {

inline int io_uring_enter(uint32_t fd, uint32_t to_submit, uint32_t min_complete,
                          uint32_t flags, sigset_t *sig, size_t sz) {

    int ret = -1;
#if defined(__x86_64__)
    register uint32_t r10 asm("r10") = flags;
    register sigset_t *r8 asm("r8") = sig;
    register size_t r9 asm("r9") = sz;
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
    IOUring(uint32_t entries);
    ~IOUring() = default;
    IOUring(const IOUring &) = delete;
    IOUring(IOUring &&ref);

    template <typename Rep, typename Period>
    int enter_timeout(std::chrono::duration<Rep, Period> timeout);
    int enter();
    int enter_wait();

    bool sq_full() const;
    void sq_ensure_space();


    io_uring_sqe &sq_slot();

    void sq_push_accept(int fd, uint64_t user_data);
    void sq_push_read(int fd, uint8_t *buf, uint32_t size, uint64_t user_data);
    void sq_push_pread(int fd, size_t pos, uint8_t *buf, uint32_t size,
                           uint32_t flags, uint64_t user_data);
    void sq_push_write(int fd, uint8_t *buf, uint32_t size, uint64_t user_data);
    void sq_push_pwrite(int fd, size_t pos, const uint8_t *buf, uint32_t size,
                            uint32_t flags, uint64_t user_data);

    void sq_push_send(int fd, uint8_t *buf, uint32_t size, uint64_t user_data);
    void sq_push_recv(int fd, uint8_t *buf, uint32_t size, uint64_t user_data);
    void sq_push_connect(int fd, sockaddr *addr, size_t sockaddr_size,
                         uint64_t user_data);
    void sq_push_socket(int domain, int type, int protocol, uint64_t user_data);
    void sq_push_shutdown(int fd, int how, uint64_t user_data);
    void sq_push_openat(const char *path, uint32_t flags, uint32_t mode, uint64_t user_data);
    void sq_push_close(int fd, uint64_t user_data);

    void sq_start_push();
    void sq_end_push();

    io_uring_cqe *cq_pop();
    void cq_start_pop();
    void cq_end_pop();

private:
    class MmapDeleter {
    public:
        MmapDeleter() {}
        MmapDeleter(size_t size) : size_(size) {}

        void operator()(void *ptr) { munmap(ptr, size_); }

    private:
        size_t size_ = 0;
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
    uint32_t sq_size_ = 0;
    uint32_t sq_mask_ = 0;
    uint32_t sq_new_tail_ = 0;

    // CQ ints
    uint32_t cq_size_ = 0;
    uint32_t cq_mask_ = 0;
    uint32_t cq_new_head_ = 0;

    // SQ ptrs
    std::unique_ptr<void, MmapDeleter> sq_ptr_;
    std::unique_ptr<io_uring_sqe[], MmapDeleter> sq_sqes_;
    uint32_t *sq_array_ = nullptr;
    uint32_t *sq_head_ = nullptr;
    uint32_t *sq_tail_ = nullptr;

    // CQ ptrs
    std::unique_ptr<void, MmapDeleter> cq_ptr_;
    io_uring_cqe *cq_array_ = nullptr;
    uint32_t *cq_head_ = nullptr;
    uint32_t *cq_tail_ = nullptr;
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
    io_uring_getevents_arg arg{};
    arg.ts = reinterpret_cast<uint64_t>(&ts);
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
