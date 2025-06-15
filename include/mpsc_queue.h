#ifndef MPSC_QUEUE_H_INCLUDED
#define MPSC_QUEUE_H_INCLUDED

#include <mutex>
#include <queue>
#include <vector>

#include <sys/eventfd.h>

template <typename T>
class MPSCQueue {
public:
    MPSCQueue() = default;

    MPSCQueue(MPSCQueue &&other) {
        std::lock_guard<std::mutex> l(other.mtx_);
        queue_ = std::move(other.queue_);
    }

    void push(T item) {
        std::lock_guard<std::mutex> l(mtx_);
        queue_.push(item);
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

private:
    std::queue<T> queue_;
    std::mutex mtx_;
};

#endif
