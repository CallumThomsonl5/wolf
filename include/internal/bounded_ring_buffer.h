#pragma once

#include <cstdint>
#include <cassert>

namespace wolf::internal {

template <typename T, uint32_t S>
class BoundedRingBuffer {
    static_assert(S != 0 && ((S - 1) & S) == 0, "S must be a non-zero power of 2");
public:
    void push(const T& item) {
        assert(!full());

        uint32_t index = head_ & (size_ - 1);
        inline_buffer_[index] = item;
        head_++;
    }

    void pop() { tail_++; }

    T &peek() {
        assert(!empty());

        uint32_t index = tail_ & (size_ - 1);
        return inline_buffer_[index];
    }

    [[nodiscard]] bool empty() const { return head_ == tail_; }
    [[nodiscard]] bool full() const { return ((head_ + 1) & (size_ - 1)) == (tail_ & (size_ - 1)); }

    void clear() {
        head_ = 0;
        tail_ = 0;
        size_ = S;
    }

private:
    T inline_buffer_[S];
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
    uint32_t size_ = S;
};

} // namespace wolf::internal

