#pragma once

#include <cstdint>
#include <algorithm>

template <typename T, std::uint32_t S>
class RingBuffer {
    static_assert(S != 0 && ((S - 1) & S) == 0, "S must be a non-zero power of 2");
public:
    RingBuffer() = default; 

    RingBuffer(const RingBuffer<T, S> &b) = delete;
    RingBuffer(RingBuffer<T, S> &&b) noexcept : overflow_buffer_(b.overflow_buffer_), head_(b.head_), tail_(b.tail_), size_(b.size_), overflow_(b.overflow_) {
        if (!overflow_) {
            std::copy(b.inline_buffer_, b.inline_buffer_ + S, inline_buffer_);
        }

        b.overflow_buffer_ = nullptr;
        b.overflow_ = false;
        b.size_ = S;
        b.head_ = 0;
        b.tail_ = 0;
    }

    ~RingBuffer() {
        if (overflow_) {
            delete[] overflow_buffer_;
        }
    }

    void push(T item) {
        if (full()) {
            grow();
        }

        std::uint32_t index = head_ & (size_ - 1);
        if (overflow_) [[unlikely]] {
            overflow_buffer_[index] = item;
        } else {
            inline_buffer_[index] = item;
        }
        head_++;
    }

    void pop() { tail_++; }

    T &peek() {
        std::uint32_t index = tail_ & (size_ - 1);

        if (overflow_) [[unlikely]] {
            return overflow_buffer_[index];
        } else {
            return inline_buffer_[index];
        }
    }

    [[nodiscard]] bool empty() const { return head_ == tail_; }

    void clear() {
        if (overflow_) {
            delete[] overflow_buffer_;
            overflow_buffer_ = nullptr;
        }

        head_ = 0;
        tail_ = 0;
        size_ = S;
        overflow_ = false;
    }

private:
    [[nodiscard]] bool full() const { return ((head_ + 1) & (size_ - 1)) == (tail_ & (size_ - 1)); }

    void grow() {
        std::uint32_t new_size = size_ * 2;
        T *new_buffer = new T[new_size]();
        std::uint32_t count = head_ - tail_;

        if (overflow_) {
            for (std::uint32_t i = 0; i < count; i++) {
                new_buffer[i] = overflow_buffer_[(tail_ + i) & (size_ - 1)];
            }
            delete[] overflow_buffer_;
        } else {
            for (std::uint32_t i = 0; i < count; i++) {
                new_buffer[i] = inline_buffer_[(tail_ + i) & (size_ - 1)];
            }
            overflow_ = true;
        }

        overflow_buffer_ = new_buffer;
        tail_ = 0;
        head_ = count;
        size_ = new_size;
    }

    T inline_buffer_[S];
    T *overflow_buffer_ = nullptr;
    std::uint32_t head_ = 0;
    std::uint32_t tail_ = 0;
    std::uint32_t size_ = S;
    bool overflow_ = false;
};
