#pragma once
#include <atomic>
#include <cstddef>
#include <vector>

// Single-Producer/Single-Consumer lock-free ring buffer.
//
// Design:
//  - Capacity is rounded up to a power of two so index wrap-around is a
//    cheap bitmask instead of a modulo.
//  - head_ is only written by the consumer, tail_ only by the producer.
//    Each side only *reads* the other's atomic, so there is no CAS/lock
//    needed on the hot path -- this is what makes it appropriate for the
//    network-thread -> matching-thread handoff described in the guide.
//  - Padding (alignas 64) keeps head_ and tail_ on separate cache lines
//    to avoid false sharing between the producer and consumer cores.
template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(size_t capacity) {
        size_t cap = 1;
        while (cap < capacity) cap <<= 1;
        mask_ = cap - 1;
        buffer_.resize(cap);
    }

    // Producer side. Returns false if the queue is full.
    bool push(const T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next = (tail + 1) & mask_;
        if (next == head_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false if the queue is empty.
    bool pop(T& out) {
        const size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        out = buffer_[head];
        head_.store((head + 1) & mask_, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

private:
    std::vector<T> buffer_;
    size_t mask_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};
