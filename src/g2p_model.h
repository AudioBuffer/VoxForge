// VoxSynth - 神经网络 G2P 推理（读取 tools/py/train_g2p.py 导出的 VXGP 模型）
//
// 输入：上下文汉字（窗口 ±2）
// 输出：拼音音节 + 声调
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace vx {

class G2PModel {
public:
    bool load(const std::string& path) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        char magic[4];
        int dims[4]{}, nSyl = 0;
        if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "VXGP", 4) != 0) { std::fclose(f); return false; }
        if (std::fread(dims, 4, 4, f) != 4) { std::fclose(f); return false; }
        emb_ = dims[0]; win_ = dims[1]; h1_ = dims[2]; h2_ = dims[3];
        if (std::fread(&nSyl, 4, 1, f) != 1) { std::fclose(f); return false; }
        syl_.resize((size_t)nSyl);
        for (int i = 0; i < nSyl; ++i) {
            unsigned char len = 0;
            if (std::fread(&len, 1, 1, f) != 1) { std::fclose(f); return false; }
            syl_[(size_t)i].resize(len);
            if (len && std::fread(&syl_[(size_t)i][0], 1, len, f) != len) { std::fclose(f); return false; }
        }
        auto readMat = [&](int r, int c) {
            std::vector<float> m((size_t)r * c);
            if (std::fread(m.data(), 4, m.size(), f) != m.size()) m.clear();
            return m;
        };
        W1_ = readMat(emb_ * win_, h1_); b1_ = readMat(1, h1_);
        W2_ = readMat(h1_, h2_);         b2_ = readMat(1, h2_);
        Ws_ = readMat(h2_, nSyl);        bs_ = readMat(1, nSyl);
        Wt_ = readMat(h2_, 5);           bt_ = readMat(1, 5);
        std::fclose(f);
        ok_ = !Wt_.empty() && (int)W1_.size() == emb_ * win_ * h1_;
        return ok_;
    }

    bool ok() const { return ok_; }

    // ctx: 汉字序列；pos: 目标字下标。输出音节与声调（1..4，5=轻声）
    bool predict(const std::vector<std::string>& ctx, int pos, std::string& syl, int& tone) const {
        if (!ok_ || ctx.empty()) return false;
        const int in = emb_ * win_;
        std::vector<float> x((size_t)in, 0.0f);
        for (int k = 0; k < win_; ++k) {
            int i = pos + k - win_ / 2;
            if (i < 0 || i >= (int)ctx.size()) i = pos;
            const int h = charId(ctx[(size_t)i]);
            for (int d = 0; d < emb_; ++d)
                x[(size_t)(k * emb_ + d)] = (((h * (d + 1)) >> 7) & 1) ? 1.0f : -1.0f;
        }
        std::vector<float> a1((size_t)h1_), a2((size_t)h2_);
        for (int j = 0; j < h1_; ++j) {
            float s = b1_[(size_t)j];
            for (int i = 0; i < in; ++i) s += W1_[(size_t)i * h1_ + j] * x[(size_t)i];
            a1[(size_t)j] = s > 0 ? s : 0;
        }
        for (int j = 0; j < h2_; ++j) {
            float s = b2_[(size_t)j];
            for (int i = 0; i < h1_; ++i) s += W2_[(size_t)i * h2_ + j] * a1[(size_t)i];
            a2[(size_t)j] = s > 0 ? s : 0;
        }
        int bestS = 0; float best = -1e30f;
        for (int c = 0; c < (int)syl_.size(); ++c) {
            float s = bs_[(size_t)c];
            for (int i = 0; i < h2_; ++i) s += Ws_[(size_t)i * syl_.size() + c] * a2[(size_t)i];
            if (s > best) { best = s; bestS = c; }
        }
        int bestT = 0; best = -1e30f;
        for (int c = 0; c < 5; ++c) {
            float s = bt_[(size_t)c];
            for (int i = 0; i < h2_; ++i) s += Wt_[(size_t)i * 5 + c] * a2[(size_t)i];
            if (s > best) { best = s; bestT = c; }
        }
        syl = syl_[(size_t)bestS];
        tone = bestT + 1;
        return true;
    }

private:
    static int charId(const std::string& s) {
        uint32_t h = 2166136261u;
        for (unsigned char c : s) { h ^= c; h *= 16777619u; }
        return (int)(h & 0x7fffffff) % 65536;
    }

    int emb_{0}, win_{0}, h1_{0}, h2_{0};
    std::vector<std::string> syl_;
    std::vector<float> W1_, b1_, W2_, b2_, Ws_, bs_, Wt_, bt_;
    bool ok_{false};
};

}  // namespace vx
