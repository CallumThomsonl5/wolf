#ifndef WOLF_IOURING_H_INCLUDED
#define WOLF_IOURING_H_INCLUDED

#include <cstdint>
#include <exception>
#include <stack>
#include <string>

#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

class IOUringException : public std::exception {
public:
    IOUringException(const std::string &msg) : msg_(msg) {}
    const char *what() const noexcept override { return msg_.c_str(); }

private:
    const std::string msg_;
};

class IOUring {
public:
    IOUring(std::uint32_t entries);
    ~IOUring();
    IOUring(const IOUring &) = delete;
    IOUring(IOUring &&ref);

    void enter(int timeout);

    bool sq_full();
    int sq_space();
    bool cq_full();
    int cq_space();

    void sq_push();
    void cq_pop();

private:
    int fd_ = -1;
    int to_submit_ = 0;

    std::uint32_t sq_size_ = 0;
    std::uint32_t cq_size_ = 0;

    std::size_t sq_ptr_size_ = 0;
    volatile void *sq_ptr_ = nullptr;
    volatile std::uint32_t *sq_head_ = nullptr;
    volatile std::uint32_t *sq_tail_ = nullptr;
    std::uint32_t sq_mask_ = 0;
    volatile std::uint32_t *sq_array_ = nullptr;
    std::size_t sq_entries_size_ = 0;
    volatile io_uring_sqe *sq_entries_ = nullptr;
    std::stack<std::uint32_t> sq_free_list_;

    std::size_t cq_ptr_size_ = 0;
    volatile void *cq_ptr_ = nullptr;
    volatile std::uint32_t *cq_head_ = nullptr;
    volatile std::uint32_t *cq_tail_ = nullptr;
    std::uint32_t cq_mask_ = 0;
    volatile io_uring_cqe *cq_array_ = nullptr;
};

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
    fd_ = fd;

    sq_size_ = params.sq_entries;
    cq_size_ = params.cq_entries;

    for (int i = 0; i < sq_size_; i++) {
        sq_free_list_.push(i);
    }

    sq_ptr_size_ = params.sq_off.array + sq_size_ * sizeof(std::uint32_t);
    sq_ptr_ = mmap(0, sq_ptr_size_, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    if (sq_ptr_ == MAP_FAILED) {
        throw IOUringException("mmap: failed to mmap sqring");
    }
    sq_head_ = (std::uint32_t *)((std::uint8_t *)sq_ptr_ + params.sq_off.head);
    sq_tail_ = (std::uint32_t *)((std::uint8_t *)sq_ptr_ + params.sq_off.tail);
    sq_mask_ =
        *(std::uint32_t *)((std::uint8_t *)sq_ptr_ + params.sq_off.ring_mask);
    sq_array_ =
        (std::uint32_t *)((std::uint8_t *)sq_ptr_ + params.sq_off.array);

    sq_entries_size_ = sq_size_ * sizeof(struct io_uring_sqe);
    sq_entries_ =
        (io_uring_sqe *)mmap(0, sq_entries_size_, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    if (sq_entries_ == MAP_FAILED) {
        throw IOUringException("mmap: failed to mmap sqentries");
    }

    cq_ptr_size_ = params.cq_off.cqes + cq_size_ * sizeof(struct io_uring_cqe);
    cq_ptr_ = mmap(0, cq_ptr_size_, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
    if (cq_ptr_ == MAP_FAILED) {
        throw IOUringException("mmap: failed to mmap cqring");
    }
    cq_head_ = (std::uint32_t *)((std::uint8_t *)cq_ptr_ + params.cq_off.head);
    cq_tail_ = (std::uint32_t *)((std::uint8_t *)cq_ptr_ + params.cq_off.tail);
    cq_mask_ =
        *(std::uint32_t *)((std::uint8_t *)cq_ptr_ + params.cq_off.ring_mask);
    cq_array_ = (io_uring_cqe *)((std::uint8_t *)cq_ptr_ + params.cq_off.cqes);
}

inline IOUring::~IOUring() {
    if (fd_ >= 0) {
        munmap((void *)sq_ptr_, sq_ptr_size_);
        munmap((void *)sq_entries_, sq_entries_size_);
        munmap((void *)cq_ptr_, cq_ptr_size_);
        close(fd_);
    }
}

inline void IOUring::enter(int timeout) {
    syscall(SYS_io_uring_enter, fd_, to_submit_, 0, 0, 0);
    to_submit_ = 0;
}

#endif // WOLF_IOURING_H_INCLUDED
