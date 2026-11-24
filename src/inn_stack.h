// VoxSynth - 可堆叠可逆块
//
// 关键性质（用户指出的核心价值）：
//   1. 输入输出同维
//   2. forward / inverse 严格互逆
//   3. 训练时不需要保存中间激活：反向传播时用 inverse 从输出逐层还原，
//      显存占用与层数无关（只保留当前层的少量隐藏量）
//
// 每块 = 通道重排 + 仿射耦合。重排用固定置换，无参数。
#pragma once
#include <cstddef>
#include <vector>
#include "inn.h"

namespace vx {

class InvertibleStack {
public:
    void init(int dim, int layers, float scale = 0.05f, unsigned seed = 1u) {
        dim_ = dim;
        layers_ = layers;
        blocks_.assign((size_t)layers, AffineCoupling{});
        for (int i = 0; i < layers; ++i)
            blocks_[(size_t)i].init(dim, scale, seed + (unsigned)i * 977u);
        // 固定置换：奇偶交错，避免前后半段一直不动
        perm_.resize((size_t)dim);
        const int half = dim / 2;
        for (int i = 0; i < dim; ++i)
            perm_[(size_t)i] = (i % 2 == 0) ? (i / 2) : (half + i / 2);
    }

    int dim() const { return dim_; }
    int layers() const { return layers_; }

    size_t params() const {
        size_t n = 0;
        for (const auto& b : blocks_) n += b.params();
        return n;
    }

    // 训练显存统计：forward 只保存输入与输出两套缓冲，不存任何中间激活
    size_t storedActivations() const { return 2ull * (size_t)dim_; }

    void forward(const std::vector<float>& x, std::vector<float>& y) const {
        std::vector<float> a = x, t;
        for (int i = 0; i < layers_; ++i) {
            if (i % 2 == 0) permute(a);
            blocks_[(size_t)i].forward(a, t);
            a.swap(t);
        }
        y.swap(a);
    }

    // 反向：从输出逐层逆变换回输入；每步只需当前层的隐藏量，不需历史激活
    void inverse(const std::vector<float>& y, std::vector<float>& x) const {
        std::vector<float> a = y, t;
        for (int i = layers_ - 1; i >= 0; --i) {
            blocks_[(size_t)i].inverse(a, t);
            a.swap(t);
            if (i % 2 == 0) permuteInv(a);
        }
        x.swap(a);
    }

private:
    void permute(std::vector<float>& v) const {
        std::vector<float> out(v.size());
        for (size_t i = 0; i < v.size(); ++i) out[(size_t)perm_[i]] = v[i];
        v.swap(out);
    }
    // 逆置换：out[i] = v[perm[i]]
    void permuteInv(std::vector<float>& v) const {
        std::vector<float> out(v.size());
        for (size_t i = 0; i < v.size(); ++i) out[i] = v[(size_t)perm_[i]];
        v.swap(out);
    }

    int dim_ = 0, layers_ = 0;
    std::vector<AffineCoupling> blocks_;
    std::vector<int> perm_;
};

}  // namespace vx
