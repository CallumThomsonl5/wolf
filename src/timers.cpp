#include "internal/timers.h"

#include <cassert>

static std::uint32_t parent(std::uint32_t i) { return (i - 1) / 2; }
static std::uint32_t left(std::uint32_t i) { return (2 * i) + 1; }
static std::uint32_t right(std::uint32_t i) { return (2 * i) + 2; }

namespace wolf::internal {

TimerHeap::TimerHeap(std::vector<Timer> &timers) : timers_(timers) {}

bool TimerHeap::empty() const noexcept { return heap_.empty(); }

std::size_t TimerHeap::size() const noexcept { return heap_.size(); }

TimeType TimerHeap::next_expiry() const {
    assert(!empty());

    return timers_[heap_.front()].time;
}

DurationType TimerHeap::time_until_next(TimeType now) const noexcept {
    if (!empty()) {
        return next_expiry() - now;
    } else {
        return DurationType{0};
    }
}

std::uint32_t TimerHeap::pop_next() { return pop_index(0); }

void TimerHeap::cancel(std::uint32_t index) {
    pop_index(timers_[index].heap_index);
}

void TimerHeap::push(std::uint32_t index) {
    heap_.push_back(index);
    timers_[heap_.back()].heap_index = heap_.size() - 1;

    bubble_up(heap_.size() - 1);
}

void TimerHeap::swap(std::uint32_t a, std::uint32_t b) {
    timers_[heap_[a]].heap_index = b;
    timers_[heap_[b]].heap_index = a;

    std::uint32_t temp = heap_[a];
    heap_[a] = heap_[b];
    heap_[b] = temp;
}

void TimerHeap::min_heapify(std::uint32_t i) {
    std::uint32_t n = heap_.size();

    while (true) {
        std::uint32_t smallest = i;
        std::uint32_t l = left(i);
        std::uint32_t r = right(i);

        if (l < n && timers_[heap_[l]].time < timers_[heap_[smallest]].time)
            smallest = l;
        if (r < n && timers_[heap_[r]].time < timers_[heap_[smallest]].time)
            smallest = r;

        if (smallest == i)
            break;

        swap(i, smallest);

        i = smallest;
    }
}

void TimerHeap::bubble_up(std::uint32_t i) {
    while (i > 0) {
        std::uint32_t p = parent(i);
        if (timers_[heap_[i]].time >= timers_[heap_[p]].time) {
            break;
        }

        swap(i, p);
        i = p;
    }
}

std::uint32_t TimerHeap::pop_index(std::uint32_t i) {
    assert(!empty());

    std::uint32_t t = heap_[i];

    swap(i, heap_.size() - 1);
    heap_.pop_back();

    if (i == heap_.size()) {
        // i was the last element
        return t;
    }

    if (i > 0 && timers_[heap_[i]].time < timers_[heap_[parent(i)]].time) {
        bubble_up(i);
    } else {
        min_heapify(i);
    }

    return t;
}

} // namespace wolf::internal
