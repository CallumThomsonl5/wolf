#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace wolf {
using OnTimeout = void (*)(void *context);
} // namespace wolf

namespace wolf::internal {

using ClockType = std::chrono::steady_clock;
using TimeType = ClockType::time_point;
using DurationType = TimeType::duration;

struct Timer {
    TimeType time;
    DurationType interval;
    void *context;
    OnTimeout on_timeout;
    std::uint32_t generation;
    std::uint32_t heap_index;
    bool repeating;
};

class TimerHeap {
public:
    explicit TimerHeap(std::vector<Timer> &timers);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] TimeType next_expiry() const;
    [[nodiscard]] DurationType time_until_next(TimeType now) const noexcept;

    std::uint32_t pop_next();
    template <typename Visitor>
    void pop_due(TimeType now, Visitor &&visit);

    void cancel(std::uint32_t index);

    void push(std::uint32_t index);

private:
    void swap(std::uint32_t a, std::uint32_t b);
    void min_heapify(std::uint32_t i);
    void bubble_up(std::uint32_t i);
    std::uint32_t pop_index(std::uint32_t i);

    std::vector<Timer> &timers_;
    std::vector<std::uint32_t> heap_;
};

template <typename Visitor>
void TimerHeap::pop_due(TimeType now, Visitor &&visit) {
    while(!empty() && next_expiry() <= now) {
        visit(pop_next());
    }
}

} // wolf::internal
