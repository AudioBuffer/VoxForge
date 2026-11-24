// 无锁单生产者/单消费者环形缓冲（PCM 传输用）
// 只依赖 C++11 原子；放在共享内存里即可跨进程使用。
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vx {

// 头部与数据分离：头部固定大小，放在共享内存起始处
struct RingHeader {
    std::atomic<uint64_t> writePos{0};
    std::atomic<uint64_t> readPos{0};
    std::atomic<uint32_t> capacity{0};   // 以 float 为单位
    std::atomic<uint32_t> closed{0};
    std::atomic<uint32_t> sampleRate{0};
};

class RingWriter {
public:
    RingWriter(RingHeader* h, float* data) : h_(h), data_(data) {}
    // 返回实际写入的样本数（空间不足时截断）
    size_t write(const float* src, size_t n) {
        const size_t cap = h_->capacity.load(std::memory_order_relaxed);
        const uint64_t w = h_->writePos.load(std::memory_order_relaxed);
        const uint64_t r = h_->readPos.load(std::memory_order_acquire);
        const size_t freeSlots = cap - (size_t)(w - r);
        const size_t k = n < freeSlots ? n : freeSlots;
        for (size_t i = 0; i < k; ++i) data_[(w + i) % cap] = src[i];
        h_->writePos.store(w + k, std::memory_order_release);
        return k;
    }
    void close() { h_->closed.store(1, std::memory_order_release); }
private:
    RingHeader* h_;
    float* data_;
};

class RingReader {
public:
    RingReader(RingHeader* h, const float* data) : h_(h), data_(data) {}
    size_t read(float* dst, size_t n) {
        const size_t cap = h_->capacity.load(std::memory_order_relaxed);
        const uint64_t r = h_->readPos.load(std::memory_order_relaxed);
        const uint64_t w = h_->writePos.load(std::memory_order_acquire);
        const size_t avail = (size_t)(w - r);
        const size_t k = n < avail ? n : avail;
        for (size_t i = 0; i < k; ++i) dst[i] = data_[(r + i) % cap];
        h_->readPos.store(r + k, std::memory_order_release);
        return k;
    }
    bool closed() const { return h_->closed.load(std::memory_order_acquire) != 0; }
private:
    RingHeader* h_;
    const float* data_;
};

}  // namespace vx
