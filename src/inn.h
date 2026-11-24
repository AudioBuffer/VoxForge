// VoxSynth - 可逆耦合层（INN）
//
// 用途：潜空间内的精修/去噪网络。仿射耦合层严格可逆，
// 编码器与解码器只需存一份权重（反向即解码），省一半参数。
//
// 数学：
//   y1 = x1
//   y2 = x2 * exp(s(x1)) + t(x1)
// 反向：
//   x2 = (y2 - t(y1)) * exp(-s(y1))
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vx {

// 极简线性层：y = W x + b（W 行主序，rows×cols）
struct Linear {
    int rows = 0, cols = 0;
    std::vector<float> w, b;

    void init(int r, int c, float scale, unsigned seed);
    void forward(const float* x, float* y) const;
    void backwardT(const float* dy, float* dx) const;   // 用于反向传播（训练期）
};

inline void Linear::init(int r, int c, float scale, unsigned seed) {
    rows = r; cols = c;
    w.assign((size_t)r * c, 0.0f);
    b.assign((size_t)r, 0.0f);
    unsigned s = seed ? seed : 1u;
    for (auto& v : w) {
        s = s * 1664525u + 1013904223u;
        v = ((float)((s >> 8) & 0xFFFF) / 65535.0f - 0.5f) * 2.0f * scale;
    }
}

inline void Linear::forward(const float* x, float* y) const {
    for (int r = 0; r < rows; ++r) {
        float acc = b[(size_t)r];
        const float* row = &w[(size_t)r * (size_t)cols];
        for (int c = 0; c < cols; ++c) acc += row[c] * x[c];
        y[r] = acc;
    }
}

inline void Linear::backwardT(const float* dy, float* dx) const {
    for (int c = 0; c < cols; ++c) dx[c] = 0.0f;
    for (int r = 0; r < rows; ++r) {
        const float* row = &w[(size_t)r * (size_t)cols];
        for (int c = 0; c < cols; ++c) dx[c] += row[c] * dy[r];
    }
}

// 仿射耦合层：维度 dim（偶数），前一半不动，后一半由前一半调制
class AffineCoupling {
public:
    void init(int dim, float scale = 0.05f, unsigned seed = 1u) {
        dim_ = dim;
        half_ = dim / 2;
        // 小网络：half -> hidden -> half（输出 s 与 t）
        hidden_ = std::max(16, half_);
        w1_.init(hidden_, half_, scale, seed);
        w2_.init(half_ * 2, hidden_, scale, seed * 7u + 3u);
    }
    int dim() const { return dim_; }

    // 正向：x -> y
    void forward(const std::vector<float>& x, std::vector<float>& y) const {
        y = x;
        std::vector<float> h((size_t)hidden_, 0.0f), out((size_t)half_ * 2, 0.0f);
        w1_.forward(x.data(), h.data());
        for (float& v : h) v = std::max(0.0f, v);          // ReLU
        w2_.forward(h.data(), out.data());
        for (int i = 0; i < half_; ++i) {
            const float s = std::tanh(out[(size_t)i]) * 1.5f;     // log-scale
            const float t = out[(size_t)(half_ + i)];
            y[(size_t)(half_ + i)] = x[(size_t)(half_ + i)] * std::exp(s) + t;
        }
    }

    // 反向：y -> x（严格逆）
    void inverse(const std::vector<float>& y, std::vector<float>& x) const {
        x = y;
        std::vector<float> h((size_t)hidden_, 0.0f), out((size_t)half_ * 2, 0.0f);
        w1_.forward(y.data(), h.data());
        for (float& v : h) v = std::max(0.0f, v);
        w2_.forward(h.data(), out.data());
        for (int i = 0; i < half_; ++i) {
            const float s = std::tanh(out[(size_t)i]) * 1.5f;
            const float t = out[(size_t)(half_ + i)];
            x[(size_t)(half_ + i)] = (y[(size_t)(half_ + i)] - t) * std::exp(-s);
        }
    }

    size_t params() const { return w1_.w.size() + w1_.b.size() + w2_.w.size() + w2_.b.size(); }

private:
    int dim_ = 0, half_ = 0, hidden_ = 0;
    Linear w1_, w2_;
};

}  // namespace vx
