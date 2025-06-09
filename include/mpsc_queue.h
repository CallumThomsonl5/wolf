#ifndef MPSC_QUEUE_H_INCLUDED
#define MPSC_QUEUE_H_INCLUDED

#include <cstring>
#include <format>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <vector>

#include <sys/eventfd.h>

template <typename T>
class MPSCQueue {
public:
    explicit MPSCQueue(int wake_fd) : wake_fd_(wake_fd) {}

    MPSCQueue(MPSCQueue &&other) : wake_fd_(other.wake_fd_) {
        std::lock_guard<std::mutex> l(other.mtx_);
        queue_ = std::move(other.queue_);
    }

    void push(T item) {
        std::lock_guard<std::mutex> l(mtx_);
        queue_.push(item);
        if (eventfd_write(wake_fd_, 1) < 0) {
            int e = errno;
            throw std::runtime_error(std::format(
                "eventfd_write failed with error: {}", strerror(e)));
        }
    }

    std::vector<T> drain() {
        std::queue<T> local;
        {
            std::lock_guard<std::mutex> l(mtx_);
            std::swap(local, queue_);
        }

        std::vector<T> items;
        while (!local.empty()) {
            items.push_back(local.front());
            local.pop();
        }
        return items;
    }

    int wake_fd() const { return wake_fd_; }

private:
    std::queue<T> queue_;
    std::mutex mtx_;
    int wake_fd_;
};

#endif
