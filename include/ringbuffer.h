#pragma once

#include <cstdint>

template <typename T>
class RingBuffer {
    static constexpr int INITIAL_SIZE = 8;

public:
    RingBuffer() : size_(INITIAL_SIZE) { buffer_ = new T[INITIAL_SIZE](); }

    RingBuffer(const RingBuffer<T> &b) = delete;
    RingBuffer(RingBuffer<T> &&b): buffer_(b.buffer_), size_(b.size_), head_(b.head_), tail_(b.tail_) {
        b.buffer_ = nullptr; 
        b.size_ = 0;
        b.head_ = 0;
        b.tail_ = 0;
    }

    ~RingBuffer() { delete[] buffer_; }

    void push(T item) {
        if (full()) {
            grow();
        }

        std::uint32_t index = head_ & (size_ - 1);
        buffer_[index] = item;
        head_++;
    }

    void pop() { tail_++; }

    T &peek() {
        std::uint32_t index = tail_ & (size_ - 1);
        return buffer_[index];
    }

    bool empty() const { return head_ == tail_; }

    void clear() {
        head_ = 0;
        tail_ = 0;
    }

private:
    bool full() const { return ((head_ + 1) & (size_ - 1)) == (tail_ & (size_ - 1)); }

    void grow() {
        std::uint32_t new_size = size_ * 2;
        T *new_buffer = new T[new_size]();
        std::uint32_t count = head_ - tail_;
        for (std::uint32_t i = 0; i < count; i++) {
            new_buffer[i] = buffer_[(tail_ + i) & (size_ - 1)];
        }

        delete[] buffer_;
        buffer_ = new_buffer;
        tail_ = 0;
        head_ = count;
        size_ = new_size;
    }

    T *buffer_ = nullptr;
    std::uint32_t head_ = 0;
    std::uint32_t tail_ = 0;
    std::uint32_t size_ = INITIAL_SIZE;
};
